using System.Diagnostics;
using System.Net;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using HellForge.LocalBackend;

static class RequisitionTests
{
    const uint TokenWeapon = 0x12345678, BaseWeapon = 0x45323D4A, Attachment = 245428325;

    public static async Task Run(string root, Action<bool, string> check, string? nativeFixture = null)
    {
        var run = Path.Combine(root, "token-session");
        var evidence = Path.Combine(run, "backend");
        Directory.CreateDirectory(evidence);
        var registeredPath = Path.Combine(run, "registered-items.json");
        await File.WriteAllTextAsync(registeredPath,
            new JsonObject { ["items"] = new JsonArray(new JsonObject { ["mixId"] = TokenWeapon, ["copyOf"] = BaseWeapon }) }.ToJsonString());
        var registered = new RegisteredItems(registeredPath);
        var token = RegisteredItems.WeaponChild(Attachment, TokenWeapon);
        using var profile = ProfileStore.Open(Path.Combine(root, "token-profile"));
        profile.Ownership = new OwnershipFilter(PaidContentSet.Load(), null);
        registered.OwnsBase = profile.Ownership.IsAllowed;
        profile.TryChange((_, inventory) =>
        {
            ProfileStore.AddItem(inventory, MissionService.RequisitionMixId, 5000 - ProfileStore.Held(inventory, MissionService.RequisitionMixId));
            return true;
        });
        await using var app = LocalBackend.Create(new BackendSettings(0, evidence, LoginMode.LocalSession)
            { StartupFixtures = true, Profile = profile, RegisteredItems = registered });
        await app.StartAsync();
        try
        {
            var origin = new Uri(app.Urls.Single());
            await File.WriteAllTextAsync(Path.Combine(evidence, "ready.json"),
                new JsonObject { ["urls"] = new JsonArray(origin.ToString().TrimEnd('/')) }.ToJsonString());
            using var client = new HttpClient(new HttpClientHandler { UseProxy = false, AllowAutoRedirect = false })
                { BaseAddress = origin, Timeout = TimeSpan.FromSeconds(10) };
            using var catalogue = JsonDocument.Parse(await client.GetStringAsync("/Progression/items"));
            var entry = catalogue.RootElement.EnumerateArray().Single(e => e.GetProperty("mixId").GetUInt32() == token);
            check(entry.GetProperty("parentId").GetUInt32() == TokenWeapon &&
                entry.GetProperty("buyPrice").GetArrayLength() == 1 &&
                entry.GetProperty("buyPrice")[0].GetProperty("mixId").GetUInt32() == MissionService.RequisitionMixId &&
                entry.GetProperty("buyPrice")[0].GetProperty("amount").GetInt64() == 1000, "Fake token price is not exactly 1000 requisition.");
            long Balance() => ProfileStore.Held((JsonArray)profile.Current()["inventory"]!["items"]!, MissionService.RequisitionMixId);
            async Task<(HttpStatusCode Status, string Body)> Buy()
            {
                using var response = await client.PostAsync("/Progression/buy/v2",
                    new StringContent($"{{\"mixId\":{token},\"amount\":1}}", Encoding.UTF8, "application/json"));
                return (response.StatusCode, await response.Content.ReadAsStringAsync());
            }
            if (nativeFixture != null)
            {
                var start = new ProcessStartInfo(Path.GetFullPath(nativeFixture))
                    { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
                start.ArgumentList.Add(run);
                var gameLogs = Path.Combine(run, "game");
                Directory.CreateDirectory(gameLogs);
                start.ArgumentList.Add(gameLogs);
                using var process = Process.Start(start)!;
                var output = process.StandardOutput.ReadToEndAsync();
                var error = process.StandardError.ReadToEndAsync();
                using var deadline = new CancellationTokenSource(TimeSpan.FromSeconds(45));
                try { await process.WaitForExitAsync(deadline.Token); }
                catch (OperationCanceledException) { process.Kill(entireProcessTree: true); throw; }
                Console.Write(await output);
                check(process.ExitCode == 0, "Native shop fixture failed: " + await error);
                check(Balance() == 2000, "The native mod did not persist two purchases and one deliberately lost reply.");
            }
            else
            {
                var first = await Buy();
                var second = await Buy();
                using var firstReply = JsonDocument.Parse(first.Body);
                check(first.Status == HttpStatusCode.OK && second.Status == HttpStatusCode.OK && Balance() == 3000 &&
                    firstReply.RootElement[0].GetProperty("mixId").GetUInt32() == token &&
                    firstReply.RootElement[1].GetProperty("amount").GetInt64() == -1000,
                    "Repeated fake token purchases did not deduct exactly 1000 each.");
            }
            var expectedBalance = nativeFixture != null ? 2000 : 3000;
            check(ProfileStore.Held((JsonArray)ProfileStore.Load(profile.FilePath!)["inventory"]!["items"]!, MissionService.RequisitionMixId) == expectedBalance,
                "The requisition deduction was not saved to the profile.");
            check(ProfileStore.Held((JsonArray)profile.Current()["inventory"]!["items"]!, token) == (nativeFixture != null ? 3 : 2),
                "The fake tokens were not recorded by the existing purchase route.");
            profile.TryChange((_, inventory) => { ProfileStore.AddItem(inventory, MissionService.RequisitionMixId, 500 - expectedBalance); return true; });
            check((await Buy()).Status == HttpStatusCode.BadRequest && Balance() == 500, "An unaffordable purchase succeeded.");
        }
        finally { await app.StopAsync(); }
        Console.WriteLine("PASS fake token: registered catalogue price, repeated purchases, saved requisition and insufficient funds");
    }
}
