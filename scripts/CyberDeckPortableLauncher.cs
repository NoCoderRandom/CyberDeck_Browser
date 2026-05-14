using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Text;
using System.Windows.Forms;

internal static class CyberDeckPortableLauncher
{
    [STAThread]
    private static int Main(string[] args)
    {
        string root = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        if (string.IsNullOrEmpty(root))
        {
            root = AppDomain.CurrentDomain.BaseDirectory;
        }

        string appDirectory = Path.Combine(root, "App");
        string browserPath = Path.Combine(appDirectory, "CyberDeckBrowser.exe");
        string dataDirectory = Path.Combine(root, "Data");

        if (!File.Exists(browserPath))
        {
            MessageBox.Show(
                "CyberDeckBrowser.exe was not found in the portable App folder.",
                "CyberDeck Portable",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 2;
        }

        try
        {
            Directory.CreateDirectory(dataDirectory);

            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = browserPath;
            startInfo.WorkingDirectory = appDirectory;
            startInfo.UseShellExecute = false;
            startInfo.Arguments = BuildArguments(args);
            startInfo.EnvironmentVariables["CYBERDECK_APPDATA_DIR"] = dataDirectory;

            Process.Start(startInfo);
            return 0;
        }
        catch (Exception ex)
        {
            MessageBox.Show(
                "CyberDeck Browser could not be started.\r\n\r\n" + ex.Message,
                "CyberDeck Portable",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
            return 1;
        }
    }

    private static string BuildArguments(string[] args)
    {
        if (args == null || args.Length == 0)
        {
            return string.Empty;
        }

        StringBuilder builder = new StringBuilder();
        for (int index = 0; index < args.Length; ++index)
        {
            if (index > 0)
            {
                builder.Append(' ');
            }

            builder.Append(QuoteArgument(args[index]));
        }

        return builder.ToString();
    }

    private static string QuoteArgument(string argument)
    {
        if (string.IsNullOrEmpty(argument))
        {
            return "\"\"";
        }

        bool needsQuotes = argument.IndexOfAny(new[] {' ', '\t', '\n', '\v', '"'}) >= 0;
        if (!needsQuotes)
        {
            return argument;
        }

        StringBuilder quoted = new StringBuilder();
        quoted.Append('"');
        int backslashes = 0;
        foreach (char character in argument)
        {
            if (character == '\\')
            {
                ++backslashes;
                continue;
            }

            if (character == '"')
            {
                quoted.Append('\\', backslashes * 2 + 1);
                quoted.Append('"');
                backslashes = 0;
                continue;
            }

            if (backslashes > 0)
            {
                quoted.Append('\\', backslashes);
                backslashes = 0;
            }

            quoted.Append(character);
        }

        if (backslashes > 0)
        {
            quoted.Append('\\', backslashes * 2);
        }

        quoted.Append('"');
        return quoted.ToString();
    }
}
