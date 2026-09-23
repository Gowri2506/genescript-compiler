// Function calls can be nested: any call that returns a sequence can be
// the argument of another call.
SEQUENCE dna = "ATGCGATCGATCG";
PRINT LENGTH(REVERSE(COMPLEMENT(dna)));
PRINT GC_CONTENT(REVERSE_COMPLEMENT(dna));
PRINT TRANSLATE(REVERSE_COMPLEMENT(dna));
FIND_MOTIF(REVERSE(dna), "GC");
rc = REVERSE(COMPLEMENT(dna));
PRINT rc;
