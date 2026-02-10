using System;
using System.Runtime.InteropServices;

namespace Installer
{
    public static class FilterRegistrar
    {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern IntPtr LoadLibrary(string lpFileName);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool FreeLibrary(IntPtr hModule);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string procName);

        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int DllRegUnregServerDelegate();

        public static bool RegisterFilter(string dllPath)
        {
            return CallDllServerFunction(dllPath, "DllRegisterServer");
        }

        public static bool UnregisterFilter(string dllPath)
        {
            return CallDllServerFunction(dllPath, "DllUnregisterServer");
        }

        private static bool CallDllServerFunction(string dllPath, string funcName)
        {
            IntPtr hModule = LoadLibrary(dllPath);
            if (hModule == IntPtr.Zero)
            {
                Console.WriteLine($"Error: Failed to load library {dllPath}");
                return false;
            }

            try
            {
                IntPtr pFunc = GetProcAddress(hModule, funcName);
                if (pFunc == IntPtr.Zero)
                {
                    Console.WriteLine($"Error: Function {funcName} not found in {dllPath}");
                    return false;
                }

                var func = (DllRegUnregServerDelegate)Marshal.GetDelegateForFunctionPointer(pFunc, typeof(DllRegUnregServerDelegate));
                int result = func();

                if (result == 0) // S_OK
                {
                    return true;
                }
                else
                {
                    Console.WriteLine($"Error: {funcName} failed with HRESULT 0x{result:X}");
                    return false;
                }
            }
            finally
            {
                FreeLibrary(hModule);
            }
        }
    }
}
