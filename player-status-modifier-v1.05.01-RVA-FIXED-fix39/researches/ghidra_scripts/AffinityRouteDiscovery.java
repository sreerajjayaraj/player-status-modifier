// Headless Ghidra script for Crimson Desert affinity route discovery.
// @category CrimsonDesert

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CodeUnitFormat;
import ghidra.program.model.listing.CodeUnitFormatOptions;
import ghidra.program.model.listing.CodeUnitIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.util.task.TaskMonitor;

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

public class AffinityRouteDiscovery extends GhidraScript {
    private static final List<String> TARGET_STRINGS = Arrays.asList(
        "OnFriendlyItem_Give",
        "OnFriendlyItem_Take",
        "AdditionalNpcFriendlyValue",
        "AdditionalPetFriendlyValue",
        "AIFunction_VaryFriendly",
        "AIFunction_VaryFriendlyWithLogout",
        "_friendlyDataList",
        "FriendlySaveData",
        "FriendlyChanged",
        "PetFriendlyReached",
        "eErrNoInvalidFriendlyData",
        "eErrNoCantDoUpdateFriendlyDataYet",
        "eErrNoNotEnoughFriendly",
        "_varyFriendly",
        "DropFriendlyData",
        "friendlyData",
        "Additional Friendly",
        "Additional Pet Friendly"
    );

    private static final long IMAGE_BASE = 0x140000000L;
    private PrintWriter out;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        File report = args.length > 0
            ? new File(args[0])
            : new File("ghidra_affinity_route_report.txt");

        File parent = report.getParentFile();
        if (parent != null) {
            parent.mkdirs();
        }

        try (PrintWriter writer = new PrintWriter(report, StandardCharsets.UTF_8)) {
            out = writer;
            writeReport();
        }

        println("Affinity route discovery report written to: " + report.getAbsolutePath());
    }

    private void writeReport() throws Exception {
        out.println("# Crimson Desert affinity route discovery");
        out.println();
        out.println("Program: " + currentProgram.getName());
        out.println("Image base: " + currentProgram.getImageBase());
        out.println("Language: " + currentProgram.getLanguageID());
        out.println();

        Map<Function, Set<Address>> candidateFunctions = new LinkedHashMap<>();
        Memory memory = currentProgram.getMemory();
        ReferenceManager refs = currentProgram.getReferenceManager();

        for (String target : TARGET_STRINGS) {
            monitor.checkCancelled();
            out.println("## String: " + target);

            List<Address> stringAddresses = findAsciiOccurrences(target);
            if (stringAddresses.isEmpty()) {
                out.println("not found");
                out.println();
                continue;
            }

            for (Address stringAddress : stringAddresses) {
                out.println("- string @" + fmt(stringAddress) + " RVA=" + rva(stringAddress));

                Set<Address> allXrefs = new LinkedHashSet<>();

                ReferenceIterator refIter = refs.getReferencesTo(stringAddress);
                while (refIter.hasNext()) {
                    Reference ref = refIter.next();
                    allXrefs.add(ref.getFromAddress());
                }

                for (Address ripRef : findRipRelativeRefsTo(stringAddress, 24)) {
                    allXrefs.add(ripRef);
                }

                if (allXrefs.isEmpty()) {
                    out.println("  no xrefs found");
                    continue;
                }

                for (Address xref : allXrefs) {
                    Function fn = getFunctionContaining(xref);
                    out.println("  xref @" + fmt(xref) + " RVA=" + rva(xref) +
                        " function=" + functionLabel(fn));
                    printInstructionWindow(xref, 5, 10);

                    if (fn != null) {
                        candidateFunctions
                            .computeIfAbsent(fn, ignored -> new LinkedHashSet<>())
                            .add(xref);
                    }
                }
            }
            out.println();
        }

        out.println("# Candidate functions");
        out.println();
        List<Function> functions = new ArrayList<>(candidateFunctions.keySet());
        functions.sort(Comparator.comparing(f -> f.getEntryPoint().getOffset()));

        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);

        for (Function fn : functions) {
            monitor.checkCancelled();
            out.println("## " + functionLabel(fn));
            out.println("Entry: " + fmt(fn.getEntryPoint()) + " RVA=" + rva(fn.getEntryPoint()));
            out.print("Triggered by xrefs:");
            for (Address xref : candidateFunctions.get(fn)) {
                out.print(" " + fmt(xref));
            }
            out.println();
            out.println();
            out.println("### Disassembly excerpt");
            printFunctionDisassembly(fn, 80);
            out.println();
            out.println("### Decompiled excerpt");

            DecompileResults result = decompiler.decompileFunction(fn, 45, TaskMonitor.DUMMY);
            if (result.decompileCompleted() && result.getDecompiledFunction() != null) {
                out.println("```c");
                out.println(result.getDecompiledFunction().getC());
                out.println("```");
            } else {
                out.println("decompile failed: " + result.getErrorMessage());
            }
            out.println();
        }

        decompiler.dispose();
    }

    private List<Address> findAsciiOccurrences(String target) throws Exception {
        byte[] needle = target.getBytes(StandardCharsets.US_ASCII);
        List<Address> matches = new ArrayList<>();

        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded()) {
                continue;
            }
            Address current = block.getStart();
            while (current != null && current.compareTo(block.getEnd()) <= 0) {
                monitor.checkCancelled();
                Address found = currentProgram.getMemory().findBytes(
                    current,
                    block.getEnd(),
                    needle,
                    null,
                    true,
                    monitor);
                if (found == null) {
                    break;
                }
                matches.add(found);
                current = found.add(1);
            }
        }

        return matches;
    }

    private List<Address> findRipRelativeRefsTo(Address target, int maxRefs) throws Exception {
        List<Address> refs = new ArrayList<>();
        long targetOffset = target.getOffset();
        Memory memory = currentProgram.getMemory();

        for (MemoryBlock block : memory.getBlocks()) {
            if (!block.isInitialized() || !block.isLoaded() || !block.isExecute()) {
                continue;
            }
            Address start = block.getStart();
            Address end = block.getEnd();
            long blockStart = start.getOffset();
            long blockEnd = end.getOffset();
            long scanEnd = blockEnd - 4;

            for (long offset = blockStart; offset <= scanEnd; offset++) {
                monitor.checkCancelled();
                Address dispAddress = toAddr(offset);
                int displacement;
                try {
                    displacement = memory.getInt(dispAddress);
                } catch (Exception ignored) {
                    continue;
                }

                long referenced = offset + 4L + displacement;
                if (referenced != targetOffset) {
                    continue;
                }

                Address instructionAddress = findInstructionStartNear(dispAddress);
                if (instructionAddress != null) {
                    refs.add(instructionAddress);
                } else {
                    refs.add(dispAddress);
                }

                if (refs.size() >= maxRefs) {
                    return refs;
                }
            }
        }

        return refs;
    }

    private Address findInstructionStartNear(Address operandAddress) {
        for (int back = 0; back <= 8; back++) {
            Address candidate = operandAddress.subtract(back);
            CodeUnit cu = currentProgram.getListing().getCodeUnitAt(candidate);
            if (cu != null && cu.getMinAddress().equals(candidate) &&
                cu.getMaxAddress().compareTo(operandAddress) >= 0) {
                return candidate;
            }
        }
        return null;
    }

    private void printInstructionWindow(Address center, int before, int after) {
        Function fn = getFunctionContaining(center);
        if (fn == null) {
            return;
        }

        Address start = center;
        for (int i = 0; i < before; i++) {
            CodeUnit previous = currentProgram.getListing().getCodeUnitBefore(start);
            if (previous == null || !fn.getBody().contains(previous.getMinAddress())) {
                break;
            }
            start = previous.getMinAddress();
        }

        out.println("  ```asm");
        CodeUnit cu = currentProgram.getListing().getCodeUnitAt(start);
        int count = 0;
        while (cu != null && count < before + after + 1 &&
            fn.getBody().contains(cu.getMinAddress())) {
            String marker = cu.getMinAddress().equals(center) ? "=>" : "  ";
            out.println("  " + marker + " " + fmt(cu.getMinAddress()) + "  " + cu.toString());
            cu = currentProgram.getListing().getCodeUnitAfter(cu.getMaxAddress());
            count++;
        }
        out.println("  ```");
    }

    private void printFunctionDisassembly(Function fn, int maxUnits) {
        out.println("```asm");
        CodeUnitIterator iter = currentProgram.getListing().getCodeUnits(fn.getBody(), true);
        int count = 0;
        while (iter.hasNext() && count < maxUnits) {
            CodeUnit cu = iter.next();
            out.println(fmt(cu.getMinAddress()) + "  " + cu.toString());
            count++;
        }
        if (iter.hasNext()) {
            out.println("... truncated ...");
        }
        out.println("```");
    }

    private String functionLabel(Function fn) {
        if (fn == null) {
            return "<none>";
        }
        return fn.getName() + "@" + fmt(fn.getEntryPoint());
    }

    private String fmt(Address address) {
        return "0x" + Long.toHexString(address.getOffset()).toUpperCase();
    }

    private String rva(Address address) {
        return "0x" + Long.toHexString(address.getOffset() - IMAGE_BASE).toUpperCase();
    }
}
