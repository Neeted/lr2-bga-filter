using System;
using System.Security.Principal;
using System.Windows.Forms;

namespace Installer
{
    static class Program
    {
        /// <summary>
        /// アプリケーションのメイン エントリ ポイントです。
        /// </summary>
        [STAThread]
        static void Main(string[] args)
        {
            if (args != null && args.Length > 0)
            {
                foreach (var arg in args)
                {
                    if (arg.StartsWith("/lang:", StringComparison.OrdinalIgnoreCase))
                    {
                        string lang = arg.Substring(6);
                        try
                        {
                            var culture = new System.Globalization.CultureInfo(lang);
                            System.Threading.Thread.CurrentThread.CurrentCulture = culture;
                            System.Threading.Thread.CurrentThread.CurrentUICulture = culture;
                        }
                        catch
                        {
                            // Ignore invalid culture
                        }
                    }
                }
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
        }
    }
}
