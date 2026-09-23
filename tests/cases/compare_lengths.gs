// Sequences of different lengths: the extra bases count as mutations.
SEQUENCE ref = "ATGC";
SEQUENCE longer = "ATGCAA";
SEQUENCE shorter = "ATG";
COMPARE ref WITH longer;
COMPARE ref WITH shorter;
