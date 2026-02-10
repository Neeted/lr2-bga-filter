using System;
using System.Security.AccessControl;
using System.Security.Principal;
using Microsoft.Win32;
using System.Diagnostics;

namespace Installer
{
    public static class RegistryHelper
    {
        // 32bit (WOW6432Node) レジストリを使用するための View
        private static readonly RegistryView RegistryView = RegistryView.Registry32;

        public static void SetRegistryValue(string keyPath, string name, object value, RegistryValueKind kind)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.CreateSubKey(keyPath, RegistryKeyPermissionCheck.ReadWriteSubTree))
            {
                key?.SetValue(name, value, kind);
            }
        }

        public static object GetRegistryValue(string keyPath, string name)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath))
            {
                return key?.GetValue(name);
            }
        }

        public static bool RegistryKeyExists(string keyPath)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath))
            {
                return key != null;
            }
        }

        // 所有権の取得とアクセス権の設定 (TrustedInstaller対策)
        public static void TakeOwnershipAndAllowAccess(string keyPath)
        {
            // SeTakeOwnershipPrivilege と SeRestorePrivilege を有効化
            TokenManipulator.AddPrivilege(TokenManipulator.SE_TAKE_OWNERSHIP_NAME);
            TokenManipulator.AddPrivilege(TokenManipulator.SE_RESTORE_NAME);

            var admin = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);

            // 1. 所有権の取得 (WRITE_OWNER のみで開く)
            // SeTakeOwnershipPrivilege があれば、DACLに関わらず WRITE_OWNER アクセスで開けるはず
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath,
                RegistryKeyPermissionCheck.ReadWriteSubTree,
                RegistryRights.TakeOwnership))
            {
                if (key == null) return;

                var rs = key.GetAccessControl();
                rs.SetOwner(admin);
                key.SetAccessControl(rs);
            }

            // 2. アクセス権の設定 (所有者になったので WRITE_DAC (ChangePermissions) が可能)
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath,
                RegistryKeyPermissionCheck.ReadWriteSubTree,
                RegistryRights.ChangePermissions | RegistryRights.ReadPermissions))
            {
                if (key == null) return;

                var rs = key.GetAccessControl();
                var rule = new RegistryAccessRule(admin,
                    RegistryRights.FullControl,
                    InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                    PropagationFlags.None,
                    AccessControlType.Allow);

                rs.AddAccessRule(rule);
                key.SetAccessControl(rs);
            }
        }

        public static void BackupRegistryKey(string rootKey, string subKey, string backupFilePath)
        {
            // reg.exe export を使用
            // rootKey: HKLM 等, subKey: Software\...
            // 32bit キーを指す場合、WOW6432Node を含める必要があるかは実行環境依存だが、
            // 明示的に含めたパスを渡す前提とする。
            string fullPath = $"{rootKey}\\{subKey}";
            RunRegCommand($"export \"{fullPath}\" \"{backupFilePath}\" /y");
        }

        // キー内の全値を削除する (復元前のクリーンアップ用)
        public static void DeleteAllRegistryValues(string keyPath)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath, RegistryKeyPermissionCheck.ReadWriteSubTree))
            {
                if (key == null) return;

                foreach (var valueName in key.GetValueNames())
                {
                    try
                    {
                        key.DeleteValue(valueName);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine($"Warning: Failed to delete value {valueName}: {ex.Message}");
                    }
                }
            }
        }

        public static void RestoreRegistryKey(string backupFilePath)
        {
            if (System.IO.File.Exists(backupFilePath))
            {
                // reg.exe import
                RunRegCommand($"import \"{backupFilePath}\"");
            }
        }

        private static void RunRegCommand(string arguments)
        {
            var psi = new ProcessStartInfo("reg.exe", arguments)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            };
            using (var p = Process.Start(psi))
            {
                p.WaitForExit();
            }
        }

        public static void ReturnOwnershipToTrustedInstaller(string keyPath)
        {
            var admin = new SecurityIdentifier(WellKnownSidType.BuiltinAdministratorsSid, null);
            var trustedInstaller = new SecurityIdentifier("S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464");

            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath,
                RegistryKeyPermissionCheck.ReadWriteSubTree,
                RegistryRights.TakeOwnership | RegistryRights.ChangePermissions | RegistryRights.ReadPermissions))
            {
                if (key == null) return;

                var rs = key.GetAccessControl();

                // 1. Administratorsのフルコントロールを削除 (読み取り専用に戻す)
                // 追加したルールと一致するものを削除
                var ruleToRemove = new RegistryAccessRule(admin,
                    RegistryRights.FullControl,
                    InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                    PropagationFlags.None,
                    AccessControlType.Allow);

                rs.RemoveAccessRule(ruleToRemove);

                // 2. 所有者を TrustedInstaller に変更
                rs.SetOwner(trustedInstaller);

                // 3. Administrators に読み取り権限を残す (ユーザー要望)
                var readRule = new RegistryAccessRule(admin,
                    RegistryRights.ReadKey | RegistryRights.ReadPermissions,
                    InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit,
                    PropagationFlags.None,
                    AccessControlType.Allow);
                rs.AddAccessRule(readRule);

                key.SetAccessControl(rs);
            }
        }

        public static bool CurrentUserRegistryKeyExists(string keyPath)
        {
            // HKCU は 32/64bit で共有 (Softwareに関してはredirectされないことが多いが、念のためView指定)
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView))
            using (var key = baseKey.OpenSubKey(keyPath))
            {
                return key != null;
            }
        }

        public static void DeleteCurrentUserSubKeyTree(string keyPath)
        {
            using (var baseKey = RegistryKey.OpenBaseKey(RegistryHive.CurrentUser, RegistryView))
            {
                baseKey.DeleteSubKeyTree(keyPath, throwOnMissingSubKey: false);
            }
        }
    }

    public static class TokenManipulator
    {
        [System.Runtime.InteropServices.DllImport("advapi32.dll", ExactSpelling = true, SetLastError = true)]
        internal static extern bool AdjustTokenPrivileges(IntPtr htok, bool disall, ref TokPriv1Luid newst, int len, IntPtr prev, IntPtr relen);

        [System.Runtime.InteropServices.DllImport("kernel32.dll", ExactSpelling = true)]
        internal static extern IntPtr GetCurrentProcess();

        [System.Runtime.InteropServices.DllImport("advapi32.dll", ExactSpelling = true, SetLastError = true)]
        internal static extern bool OpenProcessToken(IntPtr h, int acc, ref IntPtr phtok);

        [System.Runtime.InteropServices.DllImport("advapi32.dll", SetLastError = true)]
        internal static extern bool LookupPrivilegeValue(string host, string name, ref long pluid);

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential, Pack = 1)]
        internal struct TokPriv1Luid
        {
            public int Count;
            public long Luid;
            public int Attr;
        }

        internal const int SE_PRIVILEGE_ENABLED = 0x00000002;
        internal const int TOKEN_QUERY = 0x00000008;
        internal const int TOKEN_ADJUST_PRIVILEGES = 0x00000020;
        public const string SE_TAKE_OWNERSHIP_NAME = "SeTakeOwnershipPrivilege";
        public const string SE_RESTORE_NAME = "SeRestorePrivilege";

        public static bool AddPrivilege(string privilege)
        {
            try
            {
                bool ret;
                TokPriv1Luid tp;
                IntPtr hproc = GetCurrentProcess();
                IntPtr htok = IntPtr.Zero;
                ret = OpenProcessToken(hproc, TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, ref htok);
                tp.Count = 1;
                tp.Luid = 0;
                tp.Attr = SE_PRIVILEGE_ENABLED;
                ret = LookupPrivilegeValue(null, privilege, ref tp.Luid);
                ret = AdjustTokenPrivileges(htok, false, ref tp, 0, IntPtr.Zero, IntPtr.Zero);
                return ret;
            }
            catch
            {
                return false;
            }
        }
    }
}
