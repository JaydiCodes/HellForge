# Builds HellForge's in-game editor (native/editor, docs/sdk-roadmap.md phase 5a) into artifacts/editor/hellforge-editor, a
# mod folder (manifest, DLL, Dear ImGui's licence) to copy into <installation>/mods. Dear ImGui 1.91.9b (MIT) is fetched
# from GitHub once, pinned by SHA-256, into tools/imgui; it is compiled with the editor by the project's pinned Zig.
# With -Test it also runs native/editor/test/overlay_test.cpp: the editor against a real D3D12 swap chain (then a D3D11 one) in a window of
# its own (shown for a few seconds), with F8, resizes and a screenshot check (then forced into HDR10 and scRGB, the
# colours checked PQ-encoded), and native/editor/test/local_stats_test.cpp,
# its saving into the Local Stats mod.
# Usage: scripts/Build-Editor.ps1 [-Install <installation>] [-Test [-SkipOverlayTest]]   (with -Install, the folder is copied into
# its mods, with the game icons extracted from that installation's game copy; -SkipOverlayTest leaves out overlay_test, which
# needs a GPU and a desktop to draw on, as on CI's hosted runners)
# Usage: scripts/Build-Editor.ps1 -SpendMyResources [-Test]   (standalone shop; -Test runs native/profile and D3D12/11 fixtures)
# Usage: scripts/Build-Editor.ps1 -UnbalancePass [-Test]
param([string]$Install,[switch]$Test,[switch]$SkipOverlayTest,[switch]$SpendMyResources,[switch]$UnbalancePass)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if($SpendMyResources -and ($Install -or $SkipOverlayTest)){
    throw '-SpendMyResources builds a standalone mod. Do not combine it with -Install or -SkipOverlayTest.'
}
if($UnbalancePass -and ($SpendMyResources -or $Install -or $SkipOverlayTest)){
    throw '-UnbalancePass builds a standalone mod. Do not combine it with -SpendMyResources, -Install or -SkipOverlayTest.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$project=Split-Path -Parent $PSScriptRoot
$buildTools=& (Join-Path $PSScriptRoot 'Get-BuildTools.ps1')
$zig=$buildTools.Zig
$testOut=Join-Path $project 'artifacts/editor/test'
function Build-OverlayTest {
    $minHook=$buildTools.MinHook
    New-Item -ItemType Directory $testOut -Force|Out-Null
    $hookObjects=@()
    foreach($source in @('src/buffer.c','src/hook.c','src/trampoline.c','src/hde/hde64.c')){
        $object=Join-Path $testOut ('mh_'+[IO.Path]::GetFileNameWithoutExtension($source)+'.o')
        & $zig cc -target x86_64-windows-gnu -O1 -w -I (Join-Path $minHook 'include') -c (Join-Path $minHook $source) -o $object
        if($LASTEXITCODE){throw 'MinHook did not compile.'}
        $hookObjects+=$object
    }
    & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror -Wno-unused-parameter -I (Join-Path $project 'native/mod-sdk') -I (Join-Path $minHook 'include') `
        (Join-Path $project 'native/editor/test/overlay_test.cpp') @hookObjects -ld3d11 -ld3d12 -ldxgi -lgdi32 -o (Join-Path $testOut 'overlay_test.exe')
    if($LASTEXITCODE){throw 'The overlay test did not compile.'}
}
$archive=Join-Path $project 'tools/downloads/imgui-1.91.9b.zip'
$sha='FD37507C8476A6D14CC7C4B352401F31BCBD0F0D995D35390811E968C466F46E'
if(-not(Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive).Hash -ne $sha){
    New-Item -ItemType Directory (Split-Path -Parent $archive) -Force|Out-Null
    Invoke-WebRequest -Uri 'https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip' -OutFile ($archive+'.partial') -UseBasicParsing
    if((Get-FileHash -LiteralPath ($archive+'.partial')).Hash -ne $sha){Remove-Item -LiteralPath ($archive+'.partial');throw 'Dear ImGui does not match its pinned checksum.'}
    Move-Item -LiteralPath ($archive+'.partial') -Destination $archive -Force
}
$imgui=Join-Path $project 'tools/imgui/imgui-1.91.9b'
$marker=Join-Path $project 'tools/imgui/.hellforge-archive-sha256'
if(-not(Test-Path -LiteralPath $marker) -or [IO.File]::ReadAllText($marker) -ne $sha){
    if(Test-Path -LiteralPath $imgui){Remove-Item -LiteralPath $imgui -Recurse -Force}
    New-Item -ItemType Directory (Join-Path $project 'tools/imgui') -Force|Out-Null
    [IO.Compression.ZipFile]::ExtractToDirectory($archive,(Join-Path $project 'tools/imgui'))
    [IO.File]::WriteAllText($marker,$sha)
}
$out=if($SpendMyResources){Join-Path $project 'artifacts/SpendMyResources'}elseif($UnbalancePass){
    Join-Path $project 'artifacts\UnbalancePass'
}else{Join-Path $project 'artifacts/editor/hellforge-editor'}
New-Item -ItemType Directory $out -Force|Out-Null
$sources=@('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_demo.cpp','backends/imgui_impl_win32.cpp','backends/imgui_impl_dx11.cpp','backends/imgui_impl_dx12.cpp')|ForEach-Object {Join-Path $imgui $_}
$common=@('-target','x86_64-windows-gnu','-O2','-std=c++17','-fno-exceptions','-fno-rtti','-I',(Join-Path $project 'native/mod-sdk'),'-I',$imgui,'-I',(Join-Path $imgui 'backends'),'-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS',('-ffile-prefix-map='+$project+'=.')) # source paths in the DLL are relative: it ships
# Dear ImGui as it is (its own warnings off), then the editor with the warnings every HellForge mod is built with.
$objects=Join-Path $project 'artifacts/editor/obj'
New-Item -ItemType Directory $objects -Force|Out-Null
$built=@()
# HDR: ImGui's DX11 and DX12 renderers compile their pixel shader from a fixed string. The editor supplies its own while
# the game draws in HDR (native/editor/editor.cpp, g_hellforgePixelShader), so the one line declaring that string is
# changed in a copy beside the objects; the pinned source stays as it is. A version where the line is not there once stops.
$shaderLine='static const char* pixelShader ='
$shaderHook='extern const char* g_hellforgePixelShader; const char* pixelShader = g_hellforgePixelShader != nullptr ? g_hellforgePixelShader :'
$sources=foreach($source in $sources){
    if([IO.Path]::GetFileName($source) -notin @('imgui_impl_dx11.cpp','imgui_impl_dx12.cpp')){$source;continue}
    $text=[IO.File]::ReadAllText($source)
    if(([regex]::Matches($text,[regex]::Escape($shaderLine))).Count -ne 1){throw ('The HDR shader hook does not fit '+$source+': '+$shaderLine+' is not there once.')}
    $copy=Join-Path $objects ([IO.Path]::GetFileName($source))
    [IO.File]::WriteAllText($copy,$text.Replace($shaderLine,$shaderHook))
    $copy
}
foreach($source in $sources){
    $object=Join-Path $objects ([IO.Path]::GetFileNameWithoutExtension($source)+'.o')
    & $zig c++ @common -w -c $source -o $object
    if($LASTEXITCODE){throw ('Dear ImGui did not compile: '+$source)}
    $built+=$object
}
if($UnbalancePass){
    $mod=Join-Path $project "examples\mymods\JedsDead's Unbalance Pass"
    $src=Join-Path $mod 'src'
    $frontend=Join-Path $mod 'overlay'
    $editor=Join-Path $project 'native\editor'
    $sdk=Join-Path $project 'native\mod-sdk'
    $gameplay=Join-Path $objects 'unbalance_pass.o'
    & $zig cc -target x86_64-windows-gnu -O2 -Wall -Werror -Wno-unused-parameter -I $sdk -DUNBALANCE_OVERLAY `
        -c (Join-Path $src 'unbalance_pass.c') -o $gameplay
    if($LASTEXITCODE){throw 'UnBalance Pass gameplay did not compile.'}
    $dll=Join-Path $out 'unbalance_pass.dll'
    $renderer=@('editor.cpp','icons.cpp','canvas.cpp','json.cpp','local_stats.cpp','sounds.cpp','translate.cpp')|ForEach-Object {Join-Path $editor $_}
    & $zig c++ @common -shared -Wall -Werror -Wno-unused-parameter -s -I $src -I $editor -I $frontend `
        '-DHF_OVERLAY_FRONTEND=\"unbalance_frontend.h\"' `
        @renderer (Join-Path $frontend 'unbalance_frontend.cpp') $gameplay @built `
        -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -lole32 -luser32 -o $dll
    if($LASTEXITCODE){throw 'UnBalance Pass overlay did not compile.'}
    Get-ChildItem -LiteralPath $out -Filter '*.lib' | Remove-Item
    Copy-Item -LiteralPath (Join-Path $mod 'hellforge-mod.json'),(Join-Path $mod 'hellforge-stats.json'),(Join-Path $mod 'README.md'),(Join-Path $project 'LICENSE') -Destination $out
    Copy-Item -LiteralPath (Join-Path $imgui 'LICENSE.txt') -Destination (Join-Path $out 'imgui-LICENSE.txt')
    Write-Output "Built standalone mod: $dll"
    if($Test){
        New-Item -ItemType Directory $testOut -Force|Out-Null
        $passivesTest=Join-Path $testOut 'unbalance_passives_test.exe'
        & $zig cc -target x86_64-windows-gnu -O1 -Wall -Werror -Wno-unused-parameter -I $sdk `
            (Join-Path $mod 'tests\passives_test.c') -o $passivesTest
        if($LASTEXITCODE){throw 'The UnBalance Pass fixture did not compile.'}
        & $passivesTest
        if($LASTEXITCODE){throw 'The UnBalance Pass fixture failed.'}
        $settingsTest=Join-Path $testOut 'unbalance_settings_test.exe'
        $imguiCore=@($built|Where-Object {[IO.Path]::GetFileName($_) -notlike 'imgui_impl_*'})
        & $zig c++ @common -Wall -Werror -I $sdk -I $src -I $editor `
            (Join-Path $mod 'tests\settings_test.cpp') (Join-Path $editor 'json.cpp') @imguiCore -o $settingsTest
        if($LASTEXITCODE){throw 'The UnBalance Pass settings fixture did not compile.'}
        $settingsDir=Join-Path $testOut ('unbalance-settings-'+[guid]::NewGuid())
        New-Item -ItemType Directory $settingsDir|Out-Null
        try {
            & $settingsTest $settingsDir
            if($LASTEXITCODE){throw 'The UnBalance Pass settings fixture failed.'}
        } finally {
            foreach($file in @('hellforge-jedsdeads-unbalance-pass.json','hellforge-jedsdeads-unbalance-pass.json.ingame.tmp')){
                $path=Join-Path $settingsDir $file
                if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}
            }
            if(-not (Get-ChildItem -LiteralPath $settingsDir -Force)){Remove-Item -LiteralPath $settingsDir}
        }
    }
    return
}
if($SpendMyResources){
    $mod=Join-Path $project 'examples/mymods/SpendMyResources'
    $src=Join-Path $mod 'src'
    $editor=Join-Path $project 'native/editor'
    $catalogue=Get-Content -LiteralPath (Join-Path $project 'mappings\catalog.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $extraNames=foreach($entry in ($catalogue.stratagems.PSObject.Properties | Sort-Object Name)){
        '    {{{0}u, "{1}"}},' -f ([uint32]$entry.Value.id), $entry.Name.Replace('\','\\').Replace('"','\"')
    }
    [IO.File]::WriteAllLines((Join-Path $objects 'smr_extra_names.inc'), [string[]]$extraNames)
    # Kill rewards: the hostile enemy units, sorted by resource for a binary search. SEAF and civilians are not enemies.
    $entities=(Get-Content -LiteralPath (Join-Path $project 'mappings\entities.json') -Raw -Encoding UTF8 | ConvertFrom-Json).entities
    $enemies=foreach($entry in $entities.PSObject.Properties){
        if($entry.Value.kind -ne 'enemy' -or $entry.Value.faction -eq 'super_earth'){continue}
        [pscustomobject]@{resource=[Convert]::ToUInt64($entry.Value.resource.Substring(2),16);label=$entry.Value.label}
    }
    if(-not $enemies){throw 'No enemies were found in mappings\entities.json.'}
    $enemyLines=foreach($enemy in ($enemies | Sort-Object resource)){
        '    {{UINT64_C(0x{0:X16}), "{1}"}},' -f $enemy.resource, $enemy.label.Replace('\','\\').Replace('"','\"')
    }
    [IO.File]::WriteAllLines((Join-Path $objects 'smr_enemies.inc'), [string[]]$enemyLines)
    # Helldiver upgrades: each loadout weapon's default magazines and each throwable's counts, by loadout id.
    $schema=Get-Content -LiteralPath (Join-Path $project 'src\HellForge.Core\stats-schema.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    $itemSlots=@{primary=0;secondary=1;throwable=2}
    function Get-JsonValue($object,[string]$name){ if($null -eq $object){return $null}; $p=$object.PSObject.Properties[$name]; if($p){$p.Value} }
    $helldiverItems=foreach($group in @(
        @{targets=$schema.targets.weapons;fields=@('magazine.starting_magazines','magazine.spare_magazines')},
        @{targets=$schema.targets.throwables;fields=@('throwable.starting_count','throwable.max_count','throwable.count_from_supply')})){
        foreach($entry in ($group.targets.PSObject.Properties | Sort-Object Name)){
            $slot=$itemSlots[[string](Get-JsonValue $entry.Value 'slot')]
            $item=Get-JsonValue $catalogue.items $entry.Name
            if($null -eq $slot -or -not $item -or -not (Get-JsonValue $item 'id')){continue}
            $fields=@(foreach($name in $group.fields){Get-JsonValue (Get-JsonValue $entry.Value 'fields') $name})
            if(@($fields|Where-Object{$_}).Count -ne $group.fields.Count){continue}
            $first=$fields[0]
            $delta=$first.kind -eq 'EntityDelta'
            if(@($fields|Where-Object{$_.kind -ne $first.kind -or $_.resource -ne $first.resource -or (Get-JsonValue $_ 'record') -ne (Get-JsonValue $first 'record') -or $_.storage -ne 'u32'}).Count){continue}
            if(-not $delta -and $first.kind -ne 'component'){continue}
            $parts=foreach($field in $fields){'{{"{0}", {1}}}' -f $(if($delta){$field.component}else{''}), $field.offset}
            $record=Get-JsonValue $first 'record'
            '    {{{0}u, {1}, {2}, {3}, {4}, UINT64_C({5}), {6}, {{{7}}}}},' -f $item.id, $slot, [int]$delta, [int][bool](Get-JsonValue $first 'shared'),
                $fields.Count, $first.resource, $(if($null -eq $record){0}else{$record}), ($parts -join ', ')
        }
    }
    if(-not $helldiverItems){throw 'No Helldiver upgrade items were found in the stats schema.'}
    [IO.File]::WriteAllLines((Join-Path $objects 'smr_helldiver_items.inc'), [string[]]$helldiverItems)
    $gameplay=Join-Path $objects 'spend_my_resources.o'
    & $zig cc -target x86_64-windows-gnu -O2 -Wall -Werror -Wno-unused-parameter -I (Join-Path $project 'native/mod-sdk') -I $objects `
        -c (Join-Path $src 'spend_my_resources.c') -o $gameplay
    if($LASTEXITCODE){throw 'Spend My Resources gameplay did not compile.'}
    $dll=Join-Path $out 'spend_my_resources.dll'
    $frontend=@(
        'shop\shop_frontend.cpp','shop\frontend\widgets.cpp','shop\frontend\layout.cpp',
        'shop\frontend\kill_counter.cpp','shop\frontend\checkout.cpp','shop\frontend\destroyer_panel.cpp',
        'shop\frontend\helldiver_panel.cpp','shop\frontend\equipment_panel.cpp','shop\frontend\exchange.cpp'
    )|ForEach-Object {Join-Path $src $_}
    $storage=@(
        'config\config_path.cpp','config\prices.cpp','config\upgrade_config.cpp','currency\currency_ledger.cpp'
    )|ForEach-Object {Join-Path $src $_}
    & $zig c++ @common -shared -Wall -Werror -Wno-unused-parameter -s -I $src -I $editor '-DHF_OVERLAY_FRONTEND=\"shop/shop_frontend.h\"' `
        (Join-Path $editor 'editor.cpp') (Join-Path $editor 'icons.cpp') (Join-Path $editor 'json.cpp') @frontend @storage $gameplay @built `
        -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -lole32 -luser32 -o $dll
    if($LASTEXITCODE){throw 'Spend My Resources overlay did not compile.'}
    $imageOut=Join-Path $out 'images'
    New-Item -ItemType Directory $imageOut -Force|Out-Null
    $svgNames=@('Requisition_Slip','Common_Sample_Icon','Rare_Sample_Icon','Super_Sample_Icon')
    $imageSrc=Join-Path $src 'images'
    $available=@($svgNames|Where-Object{Test-Path -LiteralPath (Join-Path $imageSrc ($_+'.svg')) -PathType Leaf})
    $svgTool=Join-Path $objects 'smr_svg_to_png.exe'
    if($available.Count -or $Test){
        & $zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -Wall -Werror -municode `
            (Join-Path $src 'svg_to_png.cpp') -ld3d11 -ldxgi -ld2d1 -lwindowscodecs -lole32 -lshlwapi -luuid -o $svgTool
        if($LASTEXITCODE){throw 'The currency SVG converter did not compile.'}
    }
    foreach($name in $svgNames){
        $source=Join-Path $imageSrc ($name+'.svg')
        $destination=Join-Path $imageOut ($name+'.png')
        if(-not(Test-Path -LiteralPath $source -PathType Leaf)){
            Write-Warning "Optional currency SVG not found: $source. Any existing PNG is retained."
            continue
        }
        $temporary=Join-Path $objects ('smr-svg-'+[guid]::NewGuid()+'.png')
        try {
            & $svgTool $source $temporary
            if($LASTEXITCODE){throw "The currency SVG could not be rendered: $source"}
            Move-Item -LiteralPath $temporary -Destination $destination -Force
        } finally {
            if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}
        }
    }
    Copy-Item -LiteralPath (Join-Path $project 'data\forge-credits\currency-icon.png') -Destination (Join-Path $imageOut 'Forge_Credit.png') -Force
    Get-ChildItem -LiteralPath $out -Filter '*.lib' | Remove-Item
    Copy-Item -LiteralPath (Join-Path $mod 'hellforge-mod.json'),(Join-Path $mod 'README.md'),(Join-Path $mod 'extra-stratagem-slot.md'),(Join-Path $mod 'currency-ledger.md'),(Join-Path $mod 'kill-rewards.md'),(Join-Path $mod 'prices.json'),(Join-Path $mod 'destroyer-upgrades.json'),(Join-Path $mod 'helldiver-upgrades.json'),(Join-Path $mod 'kill-rewards.json'),(Join-Path $project 'LICENSE') -Destination $out
    Copy-Item -LiteralPath (Join-Path $mod 'equipment-upgrades.json') -Destination $out
    Copy-Item -LiteralPath (Join-Path $imgui 'LICENSE.txt') -Destination (Join-Path $out 'imgui-LICENSE.txt')
    Write-Output "Built standalone mod: $dll"
    if($Test){
        New-Item -ItemType Directory $testOut -Force|Out-Null
        $shopTest=Join-Path $testOut 'shop_test.exe'
        & $zig cc -target x86_64-windows-gnu -O1 -Wall -Werror -Wno-unused-parameter -I (Join-Path $project 'native/mod-sdk') -I $objects `
            (Join-Path $mod 'test/shop_test.c') -o $shopTest
        if($LASTEXITCODE){throw 'The resource shop fixture did not compile.'}
        & $shopTest
        if($LASTEXITCODE){throw 'The resource shop fixture failed.'}
        $ledgerTest=Join-Path $testOut 'currency_ledger_test.exe'
        & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror -I (Join-Path $project 'native/mod-sdk') -I $editor `
            (Join-Path $mod 'test/currency_ledger_test.cpp') (Join-Path $editor 'json.cpp') -o $ledgerTest
        if($LASTEXITCODE){throw 'The currency ledger fixture did not compile.'}
        & $ledgerTest
        if($LASTEXITCODE){throw 'The currency ledger fixture failed.'}
        $pricesTest=Join-Path $testOut 'prices_test.exe'
        & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror -I (Join-Path $project 'native/mod-sdk') -I $editor -I $objects `
            (Join-Path $mod 'test/prices_test.cpp') (Join-Path $src 'config\config_path.cpp') -o $pricesTest
        if($LASTEXITCODE){throw 'The price list fixture did not compile.'}
        & $pricesTest (Join-Path $out 'prices.json') (Join-Path $out 'destroyer-upgrades.json') (Join-Path $out 'helldiver-upgrades.json') (Join-Path $out 'kill-rewards.json') (Join-Path $out 'equipment-upgrades.json')
        if($LASTEXITCODE){throw 'The price list fixture failed.'}
        $checkoutTest=Join-Path $testOut 'checkout_frontend_test.exe'
        & $zig c++ @common -Wall -Werror -Wno-unused-parameter -I $src -I $editor `
            (Join-Path $mod 'test/checkout_frontend_test.cpp') (Join-Path $editor 'icons.cpp') (Join-Path $editor 'json.cpp') @frontend @built `
            -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -lole32 -o $checkoutTest
        if($LASTEXITCODE){throw 'The checkout frontend fixture did not compile.'}
        $fixture=(New-Item -ItemType Directory -Path (Join-Path $testOut ('smr-icons-'+[guid]::NewGuid()))).FullName
        try {
            $valid=Join-Path $fixture 'valid\images'
            $partial=Join-Path $fixture 'partial\images'
            New-Item -ItemType Directory $valid,$partial -Force|Out-Null
            $fixtureSvg=Join-Path $fixture 'icon.svg'
            $fixturePng=Join-Path $fixture 'icon.png'
            [IO.File]::WriteAllText($fixtureSvg, '<svg xmlns="http://www.w3.org/2000/svg" width="100%" height="100%" viewBox="0 0 128 128"><clipPath id="clip"><rect x="0" y="0" width="48" height="64"/></clipPath><g transform="translate(16 16)" clip-path="url(#clip)"><path d="M0 0 H64 V64 H0 Z" style="fill:rgb(255,0,0)"/><path d="M32 32 H64 V64 H32 Z" style="fill:rgb(0,0,255)"/></g></svg>')
            & $svgTool $fixtureSvg $fixturePng
            if($LASTEXITCODE){throw 'The synthetic currency SVG did not render.'}
            foreach($file in @('Requisition_Slip.png','Common_Sample_Icon.png','Rare_Sample_Icon.png','Super_Sample_Icon.png','Forge_Credit.png')){
                Copy-Item -LiteralPath $fixturePng -Destination (Join-Path $valid $file)
                if($file -notin @('Common_Sample_Icon.png','Rare_Sample_Icon.png')){
                    Copy-Item -LiteralPath $fixturePng -Destination (Join-Path $partial $file)
                }
            }
            [IO.File]::WriteAllText((Join-Path $partial 'Rare_Sample_Icon.png'), 'not an image')
            foreach($bad in @('<svg', '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"></svg>')){
                [IO.File]::WriteAllText($fixtureSvg, $bad)
                $oldPreference=$ErrorActionPreference
                try {
                    $ErrorActionPreference='Continue'
                    & $svgTool $fixtureSvg (Join-Path $fixture 'invalid.png') 2>$null
                    $failed=$LASTEXITCODE
                } finally { $ErrorActionPreference=$oldPreference }
                if(-not $failed){throw 'The currency SVG converter accepted an invalid or empty image.'}
            }
            & $checkoutTest '--currency-icons' $fixture
            if($LASTEXITCODE){throw 'The Direct3D 11 currency icon fixture failed.'}
            & $checkoutTest '--currency-icons12' $fixture
            if($LASTEXITCODE){throw 'The Direct3D 12 currency icon fixture failed.'}
        } finally {
            Remove-Item -LiteralPath $fixture -Recurse -Force
        }
        & $checkoutTest (Join-Path $testOut 'destroyer-upgrades.bmp') '-' $out
        if($LASTEXITCODE){throw 'The checkout frontend fixture failed.'}
        Build-OverlayTest
        $oldD3D11=$env:HELLFORGE_EDITOR_TEST_D3D11
        $oldShop=$env:HELLFORGE_OVERLAY_TEST_SHOP
        try {
            $env:HELLFORGE_OVERLAY_TEST_SHOP='1'
            $env:HELLFORGE_EDITOR_TEST_D3D11=$null
            & (Join-Path $testOut 'overlay_test.exe') $dll (Join-Path $testOut 'shop-d3d12.bmp')
            if($LASTEXITCODE){throw 'The Direct3D 12 shop overlay test failed.'}
            $env:HELLFORGE_EDITOR_TEST_D3D11='1'
            & (Join-Path $testOut 'overlay_test.exe') $dll (Join-Path $testOut 'shop-d3d11.bmp')
            if($LASTEXITCODE){throw 'The Direct3D 11 shop overlay test failed.'}
        } finally {
            $env:HELLFORGE_EDITOR_TEST_D3D11=$oldD3D11
            $env:HELLFORGE_OVERLAY_TEST_SHOP=$oldShop
        }
    }
    return
}
$editorSources=@('editor.cpp','app.cpp','app_values.cpp','app_staging.cpp','app_widgets.cpp','app_page.cpp','app_presets.cpp','app_mods_logs.cpp','app_sync.cpp','app_lobbies.cpp','app_keys.cpp','app_browse.cpp','app_views.cpp','canvas.cpp','catalog.cpp','icons.cpp','json.cpp','local_stats.cpp','sounds.cpp','translate.cpp')|ForEach-Object{Join-Path $project ('native/editor/'+$_)}
& $zig c++ @common -shared -Wall -Werror -Wno-unused-parameter -s @editorSources @built `
    -ld3d11 -ld3d12 -ldxgi -ld3dcompiler_47 -ldwmapi -lgdi32 -lole32 -luser32 -o (Join-Path $out 'hellforge_editor.dll')
if($LASTEXITCODE){throw 'The editor did not compile.'}
Get-ChildItem -LiteralPath $out -Filter '*.lib'|Remove-Item
Copy-Item -LiteralPath (Join-Path $project 'native/editor/hellforge-mod.json') -Destination $out
# its picture on the launcher's Mods page (the manifest's "icon")
Copy-Item -LiteralPath (Join-Path $project 'native/editor/assets/editor-logo.png') -Destination (Join-Path $out 'icon.png')
# the word lists of its translations (docs/editor-translations.md), where there are any; none left from an older build,
# since Publish-HellForge.ps1 ships this folder
$translations=Join-Path $project 'native/editor/translations'
if(Test-Path -LiteralPath (Join-Path $out 'translations')){Remove-Item -LiteralPath (Join-Path $out 'translations') -Recurse -Force}
if(Test-Path -LiteralPath $translations){
    New-Item -ItemType Directory (Join-Path $out 'translations') -Force|Out-Null
    Copy-Item -Path (Join-Path $translations '*.tsv') -Destination (Join-Path $out 'translations') -Force
}
# The catalogue (HellForge's EditorCatalog.cs), and with -Install the icons and fonts from that installation's game copy (EditorIcons.cs, EditorFonts.cs:
# the player's own game files, read there; they are never part of a build anyone else gets).
$dataArgs=@('editor-data','--out',$out)
if($Install){$dataArgs+=@('--install',$Install)}
& dotnet run --project (Join-Path $project 'src/HellForge.Cli') -v q -- @dataArgs|Out-Null
if($LASTEXITCODE){throw 'The editor catalogue could not be made.'}
Copy-Item -LiteralPath (Join-Path $imgui 'LICENSE.txt') -Destination (Join-Path $out 'imgui-LICENSE.txt')
Write-Output ('Built {0} ({1:N0} KB)' -f (Join-Path $out 'hellforge_editor.dll'),((Get-Item (Join-Path $out 'hellforge_editor.dll')).Length/1KB))
if($Test -and -not $SkipOverlayTest){
    Build-OverlayTest
    # a big HD2R Editor preset, where there is one (artifacts/presets; never committed): the editor must stay fast with it
    $big=Join-Path $project 'artifacts/presets/Black-Wolfs-balance.hd2rpreset.json'
    $env:HF_TEST_BIG_PRESET=if(Test-Path -LiteralPath $big){$big}else{''}
    & (Join-Path $testOut 'overlay_test.exe') (Join-Path $out 'hellforge_editor.dll') (Join-Path $testOut 'screen.bmp') $out
    if($LASTEXITCODE){throw 'The overlay test failed.'}
    # again on a Direct3D 11 swap chain, as the game draws with --use-d3d11
    $env:HELLFORGE_EDITOR_TEST_D3D11='1'
    try { & (Join-Path $testOut 'overlay_test.exe') (Join-Path $out 'hellforge_editor.dll') (Join-Path $testOut 'screen11.bmp') $out }
    finally { Remove-Item Env:HELLFORGE_EDITOR_TEST_D3D11 }
    if($LASTEXITCODE){throw 'The overlay test failed on Direct3D 11.'}
    # HDR, forced (an SDR desktop shows the values as they are): HDR10 on Direct3D 12, whose colours the test checks are
    # PQ-encoded at 200 nits, and scRGB on Direct3D 11 (the other renderer and shader compile and draw)
    $env:HELLFORGE_EDITOR_HDR='pq:200'
    try { & (Join-Path $testOut 'overlay_test.exe') (Join-Path $out 'hellforge_editor.dll') (Join-Path $testOut 'screen-hdr.bmp') $out }
    finally { Remove-Item Env:HELLFORGE_EDITOR_HDR }
    if($LASTEXITCODE){throw 'The overlay test failed in HDR10.'}
    $env:HELLFORGE_EDITOR_HDR='scrgb:200'; $env:HELLFORGE_EDITOR_TEST_D3D11='1'
    try { & (Join-Path $testOut 'overlay_test.exe') (Join-Path $out 'hellforge_editor.dll') (Join-Path $testOut 'screen11-scrgb.bmp') $out }
    finally { Remove-Item Env:HELLFORGE_EDITOR_HDR; Remove-Item Env:HELLFORGE_EDITOR_TEST_D3D11 }
    if($LASTEXITCODE){throw 'The overlay test failed in scRGB on Direct3D 11.'}
}
if($Test){
    $testOut=Join-Path $project 'artifacts/editor/test'
    New-Item -ItemType Directory $testOut -Force|Out-Null
    & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror (Join-Path $project 'native/editor/test/local_stats_test.cpp') `
        (Join-Path $project 'native/editor/local_stats.cpp') (Join-Path $project 'native/editor/json.cpp') -o (Join-Path $testOut 'local_stats_test.exe')
    if($LASTEXITCODE){throw 'The Local Stats test did not compile.'}
    & (Join-Path $testOut 'local_stats_test.exe')
    if($LASTEXITCODE){throw 'The Local Stats test failed.'}
    & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror (Join-Path $project 'native/editor/test/translate_test.cpp') `
        (Join-Path $project 'native/editor/translate.cpp') -o (Join-Path $testOut 'translate_test.exe')
    if($LASTEXITCODE){throw 'The translation test did not compile.'}
    & (Join-Path $testOut 'translate_test.exe')
    if($LASTEXITCODE){throw 'The translation test failed.'}
    & $zig c++ -target x86_64-windows-gnu -O1 -std=c++17 -Wall -Werror (Join-Path $project 'native/editor/test/range_test.cpp') `
        -o (Join-Path $testOut 'range_test.exe')
    if($LASTEXITCODE){throw 'The range test did not compile.'}
    & (Join-Path $testOut 'range_test.exe')
    if($LASTEXITCODE){throw 'The range test failed.'}
}
if($Install){
    $target=Join-Path $Install 'mods/hellforge-editor'
    New-Item -ItemType Directory $target -Force|Out-Null
    Copy-Item -Path (Join-Path $out '*') -Destination $target -Force
    Write-Output ('Copied into '+$target)
}
