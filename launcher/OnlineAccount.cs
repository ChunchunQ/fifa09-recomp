using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace Fifa09Launcher;

/// <summary>The game identity the NOBK server hands out after a launcher login.</summary>
public sealed class OnlineIdentity
{
    [JsonPropertyName("gamertag")] public string Gamertag { get; set; } = "";
    [JsonPropertyName("xuid")] public string Xuid { get; set; } = "";
    [JsonPropertyName("country")] public string Country { get; set; } = "";
    [JsonPropertyName("token")] public string Token { get; set; } = "";
    [JsonPropertyName("server")] public string Server { get; set; } = "";
    [JsonPropertyName("relay")] public string Relay { get; set; } = "";
}

/// <summary>Thrown with a message that can be shown to the player as-is.</summary>
public sealed class OnlineLoginException : Exception
{
    public bool SignedOut { get; }
    public OnlineLoginException(string message, bool signedOut) : base(message) => SignedOut = signedOut;
}

/// <summary>
/// NOBK Online account login. The launcher talks to play.nobkonline.com directly
/// (not through Cloudflare): the server remembers the address the login came
/// from, and the lobby only lets the account in from that same address.
/// </summary>
public static class OnlineAccount
{
    public const string ApiBase = "https://play.nobkonline.com/api/account/";
    public const string RegisterUrl = "https://nobkonline.com/kayit/";
    public const string SiteUrl = "https://nobkonline.com/";

    static readonly HttpClient Http = CreateClient();

    static HttpClient CreateClient()
    {
        var c = new HttpClient { Timeout = TimeSpan.FromSeconds(12) };
        c.DefaultRequestHeaders.Add("X-Requested-With", "nobk");
        c.DefaultRequestHeaders.UserAgent.ParseAdd("NOBK-Launcher/1.0");
        return c;
    }

    public static Task<OnlineIdentity> Login(string username, string password) =>
        Post(new Dictionary<string, string> { ["username"] = username, ["password"] = password });

    /// <summary>Uses the saved token instead of the password; also unlocks this PC's address again.</summary>
    public static Task<OnlineIdentity> Refresh(string token) =>
        Post(new Dictionary<string, string> { ["token"] = token });

    /// <summary>"Log gönder": uploads the game's run.log (gzip) with a short note.</summary>
    public static async Task SendLog(string token, string note, byte[] gzipped)
    {
        HttpResponseMessage resp;
        try
        {
            using var req = new HttpRequestMessage(HttpMethod.Post, ApiBase + "log?note=" + Uri.EscapeDataString(note))
            {
                Content = new ByteArrayContent(gzipped),
            };
            req.Content.Headers.ContentType = new System.Net.Http.Headers.MediaTypeHeaderValue("application/gzip");
            req.Headers.Add("X-NOBK-Token", token);
            req.Headers.AcceptLanguage.ParseAdd(L.Lang == "tr" ? "tr" : "en");
            using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(60));
            resp = await Http.SendAsync(req, cts.Token).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
        {
            throw new OnlineLoginException(L.T("Could not reach the NOBK.Online server. Check your internet connection."), false);
        }
        if (resp.IsSuccessStatusCode) return;
        string text = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
        string msg = L.F("Sign-in failed (HTTP {0}).", (int)resp.StatusCode);
        try
        {
            using var doc = JsonDocument.Parse(text);
            if (doc.RootElement.TryGetProperty("error", out var e) && e.GetString() is { Length: > 0 } m) msg = m;
        }
        catch (JsonException) { }
        throw new OnlineLoginException(msg, (int)resp.StatusCode == 401);
    }

    static async Task<OnlineIdentity> Post(Dictionary<string, string> body)
    {
        HttpResponseMessage resp;
        try
        {
            // StringContent sends a Content-Length (JsonContent would send the body chunked).
            using var req = new HttpRequestMessage(HttpMethod.Post, ApiBase + "launcher")
            {
                Content = new StringContent(JsonSerializer.Serialize(body), System.Text.Encoding.UTF8, "application/json"),
            };
            req.Headers.AcceptLanguage.ParseAdd(L.Lang == "tr" ? "tr" : "en"); // server errors in the launcher's language
            resp = await Http.SendAsync(req).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException)
        {
            throw new OnlineLoginException(L.T("Could not reach the NOBK.Online server. Check your internet connection."), false);
        }

        string text = await resp.Content.ReadAsStringAsync().ConfigureAwait(false);
        if (!resp.IsSuccessStatusCode)
        {
            string msg = L.F("Sign-in failed (HTTP {0}).", (int)resp.StatusCode);
            try
            {
                using var doc = JsonDocument.Parse(text);
                if (doc.RootElement.TryGetProperty("error", out var e) && e.GetString() is { Length: > 0 } s) msg = s;
            }
            catch (JsonException) { }
            // 400 from the account check means wrong password / expired token: the saved login is no good.
            throw new OnlineLoginException(msg, (int)resp.StatusCode == 400);
        }

        var id = JsonSerializer.Deserialize<OnlineIdentity>(text);
        if (id == null || string.IsNullOrEmpty(id.Xuid) || string.IsNullOrEmpty(id.Gamertag))
            throw new OnlineLoginException(L.T("The server sent an unexpected answer."), false);
        return id;
    }
}
