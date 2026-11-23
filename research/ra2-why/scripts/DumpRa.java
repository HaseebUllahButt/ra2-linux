/*
 * Ghidra headless dump for Red Alert 2 preservation analysis.
 * Writes CSV indexes and decompiled landmark functions to
 * /path/to/user/ra2/analysis/ghidra-dump/
 */
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;

import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Program;
import ghidra.program.model.symbol.Reference;

public class DumpRa extends GhidraScript {

    private String outDir = "/path/to/user/ra2/analysis/ghidra-dump";

    private static final String[] KEYWORDS = {
        "chronosphere", "weather", "lightning", "iron curtain", "psychic",
        "yuri", "prism", "tesla", "ore purifier", "crate", "superweapon",
        "apocalypse", "kirov", "harvester", "rules", "scenario",
        "mission", ".mix", ".shp", ".vxl", ".vpl", ".csf", ".aud",
        ".wsa", ".gam", "blowfish", "westwood", "credits", "skirmish",
        "tutorial", "einstein", "tanya", "boris", "desolator",
        "paradrop", "chrono", "ion cannon", "weather storm"
    };

    private PrintWriter writer(String name) throws IOException {
        File dir = new File(outDir);
        dir.mkdirs();
        return new PrintWriter(new BufferedWriter(
            new FileWriter(new File(dir, name), false)));
    }

    private static String clean(String s) {
        if (s == null) {
            return "";
        }
        return s.replace("\"", "'").replace("\n", "\\n").replace("\r", "");
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length > 0) outDir = args[0];
        Program program = currentProgram;

        // 1. function index
        PrintWriter functionsOut = writer("functions.csv");
        functionsOut.println("address,name,size");
        LinkedHashMap<Function, Long> sizes = new LinkedHashMap<Function, Long>();
        for (Function f : program.getFunctionManager().getFunctions(true)) {
            long size = f.getBody().getNumAddresses();
            sizes.put(f, Long.valueOf(size));
            functionsOut.println(f.getEntryPoint() + ",\"" + f.getName() + "\","
                + size);
        }
        functionsOut.close();

        // 2. defined strings
        ArrayList<Data> stringData = new ArrayList<Data>();
        PrintWriter stringsOut = writer("strings.csv");
        stringsOut.println("address,value");
        for (Data data : program.getListing().getDefinedData(true)) {
            if (data.hasStringValue()) {
                stringData.add(data);
                String value = data.getValue() == null ? ""
                    : data.getValue().toString();
                stringsOut.println(data.getAddress() + ",\"" + clean(value) + "\"");
            }
        }
        stringsOut.close();

        // 3. keyword references: string -> containing function
        HashSet<Function> landmark = new HashSet<Function>();
        PrintWriter refsOut = writer("keyword_refs.csv");
        refsOut.println("string_address,string_value,function_address,function_name");
        for (Data data : stringData) {
            String value = data.getValue() == null ? "" : data.getValue().toString();
            String lower = value.toLowerCase();
            boolean hit = false;
            for (String keyword : KEYWORDS) {
                if (lower.contains(keyword)) {
                    hit = true;
                    break;
                }
            }
            if (!hit) {
                continue;
            }
            Reference[] refs = getReferencesTo(data.getAddress());
            for (Reference ref : refs) {
                Function f = getFunctionContaining(ref.getFromAddress());
                if (f != null) {
                    landmark.add(f);
                    refsOut.println(data.getAddress() + ",\""
                        + clean(value) + "\","
                        + f.getEntryPoint() + ",\"" + f.getName() + "\"");
                }
            }
        }
        refsOut.close();

        // 4. call edges from CALL instruction flows
        PrintWriter edgesOut = writer("call_edges.csv");
        edgesOut.println("caller,callee");
        for (Function f : sizes.keySet()) {
            InstructionIterator it = program.getListing()
                .getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction instr = it.next();
                String mnemonic = instr.getMnemonicString().toUpperCase();
                if (!mnemonic.startsWith("CALL")) {
                    continue;
                }
                for (Address target : instr.getFlows()) {
                    edgesOut.println(f.getEntryPoint() + "," + target);
                }
            }
        }
        edgesOut.close();

        // 5. decompile landmark functions, bounded at 400
        DecompInterface di = new DecompInterface();
        di.openProgram(program);
        PrintWriter codeOut = writer("landmark_decompiled.c");
        codeOut.println("// landmark functions decompiled by Ghidra");
        ArrayList<Function> order = new ArrayList<Function>(landmark);
        Collections.sort(order, new Comparator<Function>() {
            public int compare(Function a, Function b) {
                return a.getEntryPoint().compareTo(b.getEntryPoint());
            }
        });
        int done = 0;
        for (Function f : order) {
            if (done >= 400) {
                break;
            }
            DecompileResults res = di.decompileFunction(f, 90, monitor);
            if (res != null && res.decompileCompleted()) {
                codeOut.println();
                codeOut.println("// ==== " + f.getName() + " @ "
                    + f.getEntryPoint());
                codeOut.println(res.getDecompiledFunction().getC());
                done++;
            }
        }
        codeOut.close();
        di.dispose();
        println("DumpRa done: " + sizes.size() + " functions, "
            + stringData.size() + " strings, " + landmark.size()
            + " landmark functions, " + done + " decompiled");
    }
}
