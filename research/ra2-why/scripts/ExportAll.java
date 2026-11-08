import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayList;

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.listing.Function;

public class ExportAll extends GhidraScript {
    public void run() throws Exception {
        File outDir = new File("/path/to/user/ra2/rebuild/decomp");
        outDir.mkdirs();
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        PrintWriter index = new PrintWriter(new BufferedWriter(
            new FileWriter(new File(outDir, "index.csv"))));
        index.println("address,name,size,ok");
        ArrayList<Function> list = new ArrayList<Function>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            list.add(f);
        }
        println("exporting " + list.size() + " functions");
        int done = 0;
        for (Function f : list) {
            String addr = f.getEntryPoint().toString();
            File dir = new File(outDir, addr.substring(0, 4));
            dir.mkdirs();
            String code;
            boolean ok;
            try {
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                if (res != null && res.decompileCompleted()
                        && res.getDecompiledFunction() != null) {
                    code = res.getDecompiledFunction().getC();
                    ok = true;
                } else {
                    code = "// decompile failed\n";
                    ok = false;
                }
            } catch (Exception ex) {
                code = "// exception: " + ex + "\n";
                ok = false;
            }
            PrintWriter w = new PrintWriter(new BufferedWriter(
                new FileWriter(new File(dir, "FUN_" + addr + ".c"))));
            w.println("// " + f.getName() + " @ 0x" + addr);
            w.print(code);
            if (!code.endsWith("\n")) {
                w.println();
            }
            w.close();
            index.println(addr + "," + f.getName() + ","
                + f.getBody().getNumAddresses() + "," + ok);
            done++;
            if 
(done % 250 == 0) {
                println("exported " + done);
            }
        }
        index.close();
        di.dispose();
        println("ExportAll done: " + done);
    }
}
