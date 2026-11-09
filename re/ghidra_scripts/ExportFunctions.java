// Exports every function (entry, size, name) and the strings each one
// references, as TSV, for the function survey (docs/funktionen.md).
// Output: <dir>/functions.tsv and <dir>/string_refs.tsv, where <dir> is the
// script argument (default: the current directory).
// @category zeitbrecher_zwo
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;

public class ExportFunctions extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File dir = new File(args.length > 0 ? args[0] : ".");
        dir.mkdirs();
        try (PrintWriter fn = new PrintWriter(new File(dir, "functions.tsv"), "UTF-8");
             PrintWriter sr = new PrintWriter(new File(dir, "string_refs.tsv"), "UTF-8");
             PrintWriter cg = new PrintWriter(new File(dir, "calls.tsv"), "UTF-8")) {
            fn.println("entry\tsize\tname\tcallers\tcallees");
            sr.println("function\tinstr\tstring");
            cg.println("caller\tcallee");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            long n = 0;
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                long size = f.getBody().getNumAddresses();
                java.util.Set<Function> callees = f.getCalledFunctions(monitor);
                fn.printf("%s\t%d\t%s\t%d\t%d%n", f.getEntryPoint(), size, f.getName(),
                        f.getCallingFunctions(monitor).size(), callees.size());
                for (Function c : callees) cg.printf("%s\t%s%n", f.getEntryPoint(), c.getEntryPoint());
                InstructionIterator ii = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (ii.hasNext()) {
                    Instruction ins = ii.next();
                    for (Reference r : ins.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        Data d = getDataAt(to);
                        if (d == null || !d.hasStringValue()) continue;
                        String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                        if (s == null) continue;
                        s = s.replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n");
                        sr.printf("%s\t%s\t%s%n", f.getEntryPoint(), ins.getAddress(), s);
                    }
                }
                if (++n % 20000 == 0) println("exported " + n + " functions");
            }
            println("done: " + n + " functions");
        }
    }
}
