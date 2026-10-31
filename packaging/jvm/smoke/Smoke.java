import java.io.IOException;
import java.io.InputStream;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Enumeration;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * Loads monero_c from the JAR of the library and checks its ABI version. It is also the way to load the library: the files of the folder
 * of the platform go to one directory, because libmonero_c finds libusb next to itself.
 *
 * Run: java --enable-preview --source 21 --enable-native-access=ALL-UNNAMED Smoke.java <jar> <major.minor.patch>
 * The foreign function API is final from Java 22, and then --enable-preview and --source are not needed.
 */
public class Smoke {
    /** The name of the folder of the platform in the JAR, which is the resource prefix of JNA. */
    static String platform() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        String arch = System.getProperty("os.arch").toLowerCase(Locale.ROOT);
        boolean arm = arch.equals("aarch64") || arch.equals("arm64");
        if (os.contains("linux")) return arm ? "linux-aarch64" : "linux-x86-64";
        if (os.contains("mac")) return arm ? "darwin-aarch64" : "darwin-x86-64";
        if (os.contains("windows")) return "win32-x86-64";
        throw new IllegalStateException("unsupported system " + os + " " + arch);
    }

    static String libraryName() {
        String os = System.getProperty("os.name").toLowerCase(Locale.ROOT);
        if (os.contains("windows")) return "monero_c.dll";
        return os.contains("mac") ? "libmonero_c.dylib" : "libmonero_c.so";
    }

    public static void main(String[] args) throws Throwable {
        if (args.length != 2) {
            System.err.println("usage: Smoke <jar> <major.minor.patch>");
            System.exit(2);
        }
        String folder = platform() + "/";
        Path directory = Files.createTempDirectory("monero-native");
        try (ZipFile jar = new ZipFile(args[0])) {
            Enumeration<? extends ZipEntry> entries = jar.entries();
            while (entries.hasMoreElements()) {
                ZipEntry entry = entries.nextElement();
                if (entry.isDirectory() || !entry.getName().startsWith(folder)) continue;
                try (InputStream in = jar.getInputStream(entry)) {
                    Files.copy(in, directory.resolve(entry.getName().substring(folder.length())));
                }
            }
        }
        Path library = directory.resolve(libraryName());
        if (!Files.isRegularFile(library)) throw new IOException("no " + library);

        try (Arena arena = Arena.ofConfined()) {
            SymbolLookup lookup = SymbolLookup.libraryLookup(library, arena);
            MethodHandle getAbiVersion = Linker.nativeLinker().downcallHandle(
                lookup.find("monero_utils_get_abi_version").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.ADDRESS));
            MemorySegment version = arena.allocate(3 * ValueLayout.JAVA_INT.byteSize(), ValueLayout.JAVA_INT.byteAlignment());
            int result = (int) getAbiVersion.invokeExact(
                version.asSlice(0, 4), version.asSlice(4, 4), version.asSlice(8, 4));
            if (result != 0) throw new IllegalStateException("monero_utils_get_abi_version() returned " + result);
            String actual = version.getAtIndex(ValueLayout.JAVA_INT, 0) + "." + version.getAtIndex(ValueLayout.JAVA_INT, 1) + "."
                + version.getAtIndex(ValueLayout.JAVA_INT, 2);
            if (!actual.equals(args[1])) throw new IllegalStateException("the ABI version is " + actual + ", expected " + args[1]);
            System.out.println("ok    " + platform() + ": ABI version " + actual);
        }
    }
}
