//MultiInstructionTest

int multiInstruction1(int n0) {
    int n1 = n0 + 1;
    int n2 = n1 - 1;
    return n2;
}

int multiInstruction2(int n0) {
    int n1 = n0 + 1;
    int n2 = 1 - n1;
    return n2;
}

int multiInstruction3(int n0) {
    int n1 = n0 - 1;
    int n2 = 1 + n1;
    return n2;
}

int multiInstruction4(int n0) {
    int n1 = n0 - 1;
    int n2 = n1 + 1;
    return n2;
}