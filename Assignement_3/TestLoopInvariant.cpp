int testFunction(int x, int y) {
    int c1 = 10;              // costante loop-invariant
    int c2 = 5 + 3;           // espressione costante loop-invariant
    int a = x + y;            // loop-invariant (x, y invarianti)
    int b = a * c1;           // dipende da loop-invariant -> anche lui
    int result = 0;

    for (int i = 0; i < 20; ++i) {
        int d = c2 + b;       // loop-invariant (composta)
        int tmp = d * i;      // dipende da i -> non spostabile

        if (i % 2 == 0) {
            int e = x + y;    // loop-invariant ma ricalcolato → test duplicati
            result += tmp + e;
        } else {
            result += tmp;
        }
    }

    return result;
}

int main() {
    return testFunction(2, 3);
}