using System.Diagnostics;
using System.Text.RegularExpressions;
using Xunit;
using Xunit.Abstractions;

namespace Monero.Native.CTests;

/// <summary>
/// Runs each test that CTest lists in the build directory of monero-c, so that the runner of the IDE shows the C tests.
/// </summary>
public partial class CTestTests
{
    /// <summary>
    /// A theory that is skipped in CI, where the C tests run on their own and the native build of this solution doesn't.
    /// </summary>
    public sealed class LocalTheoryAttribute : TheoryAttribute
    {
        public LocalTheoryAttribute()
        {
            if (Environment.GetEnvironmentVariable("CI") == "true" || Environment.GetEnvironmentVariable("GITHUB_ACTIONS") == "true")
            {
                Skip = "the C tests of CI run from their own jobs";
            }
        }
    }

    private const string Missing = "(build the tests first)";
    private readonly ITestOutputHelper _output;

    public CTestTests(ITestOutputHelper output)
    {
        _output = output;
    }

    public static IEnumerable<object[]> Names()
    {
        var names = new List<string>();
        try
        {
            var result = Run(["--test-dir", BuildDirectory(), "-N"]);
            foreach (Match match in ListedTest().Matches(result.Output)) names.Add(match.Groups[1].Value);
        }
        catch (Exception)
        {
            // no ctest, or no build directory: the entry below says so
        }

        if (names.Count == 0) names.Add(Missing);
        return names.Select(name => new object[] { name });
    }

    [LocalTheory]
    [MemberData(nameof(Names))]
    public void CTest(string name)
    {
        Assert.False(name == Missing,
            $"CTest lists no tests in {BuildDirectory()}. Build the project Monero.Native first, with BUILD_TESTS on, or set MONERO_C_BUILD_DIR, and have ctest in the PATH.");

        var result = Run(["--test-dir", BuildDirectory(), "-R", $"^{Regex.Escape(name)}$", "--output-on-failure", "--no-tests=error"]);
        _output.WriteLine(result.Output);
        Assert.True(result.ExitCode == 0, $"ctest {name} exited with {result.ExitCode}");
    }

    private static string BuildDirectory()
    {
        var configured = Environment.GetEnvironmentVariable("MONERO_C_BUILD_DIR");
        if (!string.IsNullOrEmpty(configured)) return configured;

        // the repository is above the directory of the tests, whose path depends on the configuration of the build
        var directory = new DirectoryInfo(AppContext.BaseDirectory);
        while (directory != null && !File.Exists(Path.Combine(directory.FullName, "monero-c.sln"))) directory = directory.Parent;
        return Path.Combine(directory?.FullName ?? Directory.GetCurrentDirectory(), "build");
    }

    // the names of the tests of this project: letters, digits and a few separators, never an option or a path
    [GeneratedRegex(@"^[A-Za-z0-9][A-Za-z0-9_.+-]*$")]
    private static partial Regex SafeName();

    /// <summary>
    /// The ctest executable that the PATH names, as an absolute path to a file. Nothing from the environment or the test names reaches a shell.
    /// </summary>
    private static string FindCTest()
    {
        var file = OperatingSystem.IsWindows() ? "ctest.exe" : "ctest";
        foreach (var directory in (Environment.GetEnvironmentVariable("PATH") ?? string.Empty).Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries))
        {
            if (!Path.IsPathRooted(directory)) continue;
            var candidate = Path.Combine(directory, file);
            if (File.Exists(candidate)) return candidate;
        }

        throw new FileNotFoundException("ctest is not in the PATH");
    }

    private static (int ExitCode, string Output) Run(IEnumerable<string> arguments)
    {
        var list = arguments.ToList();
        // the build directory must be the one of a CMake build, and a test is selected by a name that CTest listed
        var directory = list[list.IndexOf("--test-dir") + 1];
        if (!File.Exists(Path.Combine(directory, "CMakeCache.txt"))) throw new DirectoryNotFoundException($"{directory} is not a CMake build directory");
        var selected = list.IndexOf("-R");
        if (selected >= 0 && !SafeName().IsMatch(list[selected + 1].TrimStart('^').TrimEnd('$').Replace("\\", "")))
        {
            throw new ArgumentException("the name of the test has characters that CTest never lists");
        }

        var info = new ProcessStartInfo(FindCTest())
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        foreach (var argument in list) info.ArgumentList.Add(argument);

        using var process = Process.Start(info) ?? throw new InvalidOperationException("ctest did not start");
        var output = process.StandardOutput.ReadToEndAsync();
        var error = process.StandardError.ReadToEndAsync();
        Task.WaitAll(output, error);
        process.WaitForExit();
        return (process.ExitCode, output.Result + error.Result);
    }

    [GeneratedRegex(@"^\s*Test\s+#\d+:\s+(\S+)\s*$", RegexOptions.Multiline)]
    private static partial Regex ListedTest();
}
