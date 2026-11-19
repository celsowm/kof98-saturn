// Ghidra headless script: decompile selected addresses to a text file.
// @category KOF98
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.util.task.ConsoleTaskMonitor;
import java.io.FileWriter;
import java.io.PrintWriter;

public class GhidraDecompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("usage: out.txt addr [addr...] | @addrfile");
        String out = args[0];
        java.util.List<Long> addressList = new java.util.ArrayList<>();
        for (int i = 1; i < args.length; i++) {
            String arg = args[i];
            if (arg.startsWith("@")) {
                String text = new String(java.nio.file.Files.readAllBytes(
                    java.nio.file.Paths.get(arg.substring(1))), java.nio.charset.StandardCharsets.UTF_8);
                for (String tok : text.split("\\s+")) {
                    if (!tok.isEmpty()) addressList.add(Long.decode(tok));
                }
            } else {
                addressList.add(Long.decode(arg));
            }
        }
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();
        PrintWriter w = new PrintWriter(new FileWriter(out, false));
        try {
            for (long value : addressList) {
                Address addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(value);
                Function fn = getFunctionAt(addr);
                if (fn == null) fn = getFunctionContaining(addr);
                if (fn == null) {
                    try {
                        fn = createFunction(addr, "kof_" + Long.toHexString(value));
                    } catch (Exception e) {
                        // leave as no-function; report below
                    }
                }
                String head = String.format("==== %06X : ", value);
                if (fn == null) {
                    w.println(head + "NO FUNCTION ====");
                    continue;
                }
                w.println(head + fn.getName() + " ====");
                DecompileResults res = decomp.decompileFunction(fn, 180, monitor);
                if (res.decompileCompleted()) {
                    w.println(res.getDecompiledFunction().getC());
                } else {
                    w.println("DECOMPILE FAILED: " + res.getErrorMessage());
                }
                w.println();
            }
        } finally {
            w.close();
            decomp.dispose();
        }
        println("wrote " + out);
    }
}
