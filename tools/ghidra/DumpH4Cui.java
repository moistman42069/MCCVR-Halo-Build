// Headless H4EK evidence dump. Run with analyzeHeadless -postScript DumpH4Cui.java.
// @category HaloMCCVR

import java.util.ArrayList;
import java.util.List;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;

public class DumpH4Cui extends GhidraScript {
    private static final long[] RVAS = {
        0x001F7C00L,
        0x008B63C0L,
        0x0091DD70L,
        0x0093EDD0L,
        0x009439D0L,
        0x009BE760L,
        0x009C1280L
    };

    private static final String[] NEEDLES = {
        "user_interface_render",
        "cui_render_renderer.cpp",
        "screen transform basis",
        "curvature_container_widget",
        "prop_curvature_theta",
        "prop_curvature_point_top_middle_y",
        "prop_scale",
        "prop_top",
        "prop_virtual_height",
        "prop_widget_vertical_offset_mode",
        "curved_cui",
        "Outputs data to enable parallaxing of the hud"
    };

    private static final long[] STRING_RVAS = {
        0x018BE2D8L, // prop_curvature_theta
        0x018CA2B0L, // prop_scale
        0x018CC8B0L, // prop_top
        0x018CD350L, // prop_virtual_height
        0x018CE048L, // prop_widget_vertical_offset_mode
        0x018CE8B0L, // curvature_container_widget
        0x01AD9420L, // user_interface_render
        0x01B5D4C0L, // curved_cui
        0x01B9A160L, // screen transform basis
        0x01BCF2F0L, // cui_render_renderer.cpp
        0x01C39100L  // Outputs data to enable parallaxing of the hud
    };

    private Address fromRva(long rva) {
        return currentProgram.getImageBase().add(rva);
    }

    private String functionLabel(Function f) {
        if (f == null) {
            return "<no function>";
        }
        long rva = f.getEntryPoint().subtract(currentProgram.getImageBase());
        return f.getName() + " @ RVA 0x" + Long.toHexString(rva);
    }

    private void dumpFunction(DecompInterface decompiler, long rva) throws Exception {
        Address requested = fromRva(rva);
        Function f = getFunctionContaining(requested);
        if (f == null) {
            disassemble(requested);
            f = createFunction(requested, "h4ek_rva_" + Long.toHexString(rva));
        }
        println("\n========== requested RVA 0x" + Long.toHexString(rva) +
            " -> " + functionLabel(f) + " ==========");
        if (f == null) {
            return;
        }

        println("CALLERS:");
        ReferenceIterator callers = currentProgram.getReferenceManager()
            .getReferencesTo(f.getEntryPoint());
        while (callers.hasNext()) {
            Reference ref = callers.next();
            Function caller = getFunctionContaining(ref.getFromAddress());
            println("  " + ref.getFromAddress() + " " + functionLabel(caller));
        }

        println("CALLS:");
        InstructionIterator instructions = currentProgram.getListing()
            .getInstructions(f.getBody(), true);
        while (instructions.hasNext()) {
            Instruction instruction = instructions.next();
            if (!instruction.getFlowType().isCall()) {
                continue;
            }
            for (Address target : instruction.getFlows()) {
                Function callee = getFunctionAt(target);
                println("  " + instruction.getAddress() + " -> " + target +
                    " " + functionLabel(callee));
            }
        }

        DecompileResults result = decompiler.decompileFunction(f, 120, monitor);
        if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
            println("DECOMPILE:\n" + result.getDecompiledFunction().getC());
        }
        else {
            println("DECOMPILE FAILED: " + result.getErrorMessage());
        }
    }

    private void dumpStringReferences() throws Exception {
        println("\n========== STRING REFERENCES ==========");
        for (long rva : STRING_RVAS) {
            Address address = fromRva(rva);
            println("KNOWN STRING RVA 0x" + Long.toHexString(rva) + " @ " + address);
            ReferenceIterator references = currentProgram.getReferenceManager()
                .getReferencesTo(address);
            while (references.hasNext()) {
                Reference ref = references.next();
                Function owner = getFunctionContaining(ref.getFromAddress());
                println("  XREF " + ref.getFromAddress() + " " + functionLabel(owner));
            }
            for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
                if (!block.isExecute() || block.getSize() > Integer.MAX_VALUE) {
                    continue;
                }
                byte[] bytes = new byte[(int)block.getSize()];
                int read = block.getBytes(block.getStart(), bytes);
                for (int i = 0; i + 7 <= read; ++i) {
                    int rex = bytes[i] & 0xff;
                    int opcode = bytes[i + 1] & 0xff;
                    int modrm = bytes[i + 2] & 0xff;
                    if (rex < 0x40 || rex > 0x4f || opcode != 0x8d ||
                        (modrm & 0xc7) != 0x05) {
                        continue;
                    }
                    int displacement = (bytes[i + 3] & 0xff) |
                        ((bytes[i + 4] & 0xff) << 8) |
                        ((bytes[i + 5] & 0xff) << 16) |
                        (bytes[i + 6] << 24);
                    Address instruction = block.getStart().add(i);
                    Address target = instruction.add(7L + displacement);
                    if (target.equals(address)) {
                        Function owner = getFunctionContaining(instruction);
                        println("  RIP-LEA " + instruction + " " + functionLabel(owner));
                    }
                }
            }
        }

        DataIterator dataItems = currentProgram.getListing().getDefinedData(true);
        while (dataItems.hasNext()) {
            Data data = dataItems.next();
            if (!data.hasStringValue()) {
                continue;
            }
            Address address = data.getAddress();
            String value = data.getValue().toString();
            boolean wanted = false;
            for (String needle : NEEDLES) {
                if (value.contains(needle)) {
                    wanted = true;
                    break;
                }
            }
            if (!wanted) {
                continue;
            }
            println("STRING " + address + ": " + value);
            ReferenceIterator references = currentProgram.getReferenceManager()
                .getReferencesTo(address);
            while (references.hasNext()) {
                Reference ref = references.next();
                Function owner = getFunctionContaining(ref.getFromAddress());
                println("  XREF " + ref.getFromAddress() + " " + functionLabel(owner));
            }
        }
    }

    @Override
    public void run() throws Exception {
        println("PROGRAM: " + currentProgram.getName());
        println("IMAGE BASE: " + currentProgram.getImageBase());

        DecompInterface decompiler = new DecompInterface();
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        if (!decompiler.openProgram(currentProgram)) {
            printerr("Could not initialize decompiler");
            return;
        }
        try {
            String[] arguments = getScriptArgs();
            if (arguments.length != 0) {
                for (String argument : arguments) {
                    if (argument.equalsIgnoreCase("strings")) {
                        dumpStringReferences();
                        continue;
                    }
                    String value = argument.startsWith("0x") || argument.startsWith("0X")
                        ? argument.substring(2) : argument;
                    dumpFunction(decompiler, Long.parseUnsignedLong(value, 16));
                }
            }
            else {
                for (long rva : RVAS) {
                    dumpFunction(decompiler, rva);
                }
                dumpStringReferences();
            }
        }
        finally {
            decompiler.dispose();
        }
    }
}
