// The optimizer must NOT reuse a value after its variable is reassigned.
SEQUENCE dna = "ATGCGATCGATCG";
SEQUENCE other = "GGGGCCCCAAAA";
x = GC_CONTENT(dna);
x = GC_CONTENT(other);
y = GC_CONTENT(dna);
PRINT x;
PRINT y;
// An alias shares the value: LENGTH(a) and LENGTH(dna) are the same value.
a = dna;
PRINT LENGTH(a);
PRINT LENGTH(dna);
// Printed values keep their own label: REVERSE(REVERSE(x)) at top level
// is not replaced by x.
PRINT REVERSE(REVERSE(other));
