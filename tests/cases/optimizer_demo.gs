// Everything the optimizer can do, in one program.
SEQUENCE dna = "ATGCGATCGATCG";

// CSE: GC_CONTENT(dna) is computed once; y reuses x.
x = GC_CONTENT(dna);
y = GC_CONTENT(dna);
PRINT y;

// Domain rewrite: REVERSE(COMPLEMENT(...)) -> REVERSE_COMPLEMENT(...)
PRINT REVERSE(COMPLEMENT(dna));

// Domain rewrite: LENGTH/GC_CONTENT ignore strand orientation.
PRINT LENGTH(REVERSE_COMPLEMENT(dna));

// Involution as an argument: REVERSE(REVERSE(dna)) is just dna.
PRINT TRANSLATE(REVERSE(REVERSE(dna)));

// CSE with a compiler temporary: TRANSLATE(dna) appears twice
// with no variable holding it.
PRINT TRANSLATE(dna);

// Reassignment gives x a new value number: not confused with GC_CONTENT.
x = LENGTH(dna);
PRINT x;
