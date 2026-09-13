SEQUENCE dna = "ATGCGATCGATCG";

x = GC_CONTENT(dna);
y = GC_CONTENT(dna);
PRINT x;
PRINT y;

len = LENGTH(dna);
PRINT len;
PRINT len;b

rc = REVERSE_COMPLEMENT(dna);
PRINT rc;

FIND_MOTIF(rc, "CGA");

protein = TRANSLATE(dna);
PRINT protein;

alias = dna;
PRINT alias;
