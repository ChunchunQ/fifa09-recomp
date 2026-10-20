using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Input;

namespace Fifa09Launcher;

public partial class App : Application
{
    // "--play" (used by the Steam shortcut): start the game straight away with
    // the saved settings. Hold Shift while starting to open the launcher instead.
    protected override void OnStartup(StartupEventArgs e)
    {
        bool play = e.Args.Any(a => a.Equals("--play", StringComparison.OrdinalIgnoreCase));
        bool shift = Keyboard.IsKeyDown(Key.LeftShift) || Keyboard.IsKeyDown(Key.RightShift);
        if (play && !shift && TryStartGame())
        {
            Shutdown();
            return;
        }
        base.OnStartup(e);
        new Fifa09Launcher.MainWindow().Show();
    }

    static bool TryStartGame()
    {
        var s = LauncherSettings.Load();
        if (!File.Exists(Fifa09Launcher.MainWindow.GameExe)) return false;
        if (!File.Exists(Path.Combine(s.ResolveGameFolder(), "default.xex"))) return false;
        try { s.ApplyMods(); } catch { /* start unmodded rather than not at all */ }
        // Steam "--play": refresh the NOBK login silently; offline if that fails.
        OnlineIdentity? online = null;
        if (s.PlayOnline && !string.IsNullOrEmpty(s.AccountToken))
        {
            try
            {
                online = Task.Run(() => OnlineAccount.Refresh(s.AccountToken)).GetAwaiter().GetResult();
                s.SetAccount(online);
                try { s.Save(); } catch { }
            }
            catch (OnlineLoginException ex) when (ex.SignedOut)
            {
                return false; // login expired: open the launcher so the player can sign in again
            }
            catch { online = null; }
        }
        try
        {
            var psi = new ProcessStartInfo(Fifa09Launcher.MainWindow.GameExe) { WorkingDirectory = Fifa09Launcher.MainWindow.BinDir, UseShellExecute = false };
            foreach (var a in s.BuildArgs(online)) psi.ArgumentList.Add(a);
            // Wait for the game so Steam keeps showing it as running.
            using var p = Process.Start(psi);
            p?.WaitForExit();
            return true;
        }
        catch { return false; }
    }
}
