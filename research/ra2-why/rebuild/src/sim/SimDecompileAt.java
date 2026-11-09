// Read-only recovery helper. Keep project writes disabled with analyzeHeadless -readOnly.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
public class SimDecompileAt extends GhidraScript {
    public void run() throws Exception {
        // Optional original __alloca_probe address. Apply only in the transient
        // read-only project view. Its EAX-sized stack frame defeats auto analysis.
        if (getScriptArgs().length>1) {
            Address probeAddress=toAddr(getScriptArgs()[1]);
            Function probe=getFunctionAt(probeAddress);
            if (probe==null) { disassemble(probeAddress); probe=createFunction(probeAddress,"sim_stack_probe"); }
            if (probe==null) throw new IllegalStateException("Cannot recover stack probe at "+probeAddress);
            probe.setCallFixup("alloca_probe");
        }
        Address address=toAddr(getScriptArgs()[0]);
        Function function=getFunctionAt(address);
        if (function==null) {
            Function containing=getFunctionContaining(address);
            if (containing!=null) removeFunction(containing);
            disassemble(address);
            function=createFunction(address,"sim_evidence_"+address);
        }
        if (function==null) throw new IllegalStateException("Cannot recover function at "+address);
        DecompInterface decompiler=new DecompInterface();
        decompiler.openProgram(currentProgram);
        DecompileResults result=decompiler.decompileFunction(function,120,monitor);
        if (!result.decompileCompleted()) throw new IllegalStateException(result.getErrorMessage());
        println("SIM_EVIDENCE_BEGIN "+address);
        println(result.getDecompiledFunction().getC());
        println("SIM_EVIDENCE_END "+address);
        decompiler.dispose();
    }
}
