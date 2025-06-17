
// Test Loop Fusion

// 1)
int funzione1(int n0, int n1, int n2, int n3) {
    for (int i = 0; i < 10; ++i) {
        int n2 = 4 * n0;
    }

    int n5 = 0;
    do {
        if (n0 == n1) {
            n3 = n2 + 50;
        } else {
            n3 = n2 - 50;
        }
        n5++;
    } while (n5 <= 10);

    for (int i = 0; i < 10; ++i) {
        int n2 = 2 * n0;
    }

    return 0;
}

// 2)
int funzione2(int n0, int n1, int n2, int n3) {
    int i = 0;
    int k = 0;

    if (n3 > 0) {
        do {
            int n4= 10 * n0;
            i += 1;
        } while (i < n3);
    }

    if (n3 > 0) {
        do {
            int n5 = n3;
            k += 1;
        } while (k < n3);
    }

    return 0;
}

// 3)
int funzione3(int n0, int n1, int n2, int n3) {
    int k = 0;
    do {
        if (n0 == n1) {
            n0 = n0 * 50;
        } else {
            n0 = n0 * 51;
        }
        k++;
        n1 = k * 52;
    } while (k <= 10);

    int y = 0;
    while (y < 10) {
        if (n0 == n1) {
            n0 = n0 * 50;
        } else {
            n0 = n0 * 51;
        }
        y++;
        n1 = y * 52;
    }

    return 0;
}

// 4)
int funzione4(int n0, int n1, int fin) {
    for (int i = 1; i <= fin; i++) {
        int n3 = n1 + 3;
    }

    for (int j = 0; j < fin; j++) {
        int n4 = fin + 4;
    }

    for (int n4 = 0; n4 < fin; n4++) {
        n4 = fin + 5;
    }

    return 0;
}

// 5)
int funzione5(int fin, int Array[]) {

    for (int i = 0; i < fin; i++) {
        int n2 = Array[i];
    }

    for (int j = 0; j < fin; j++) {
        Array[j] = j + 1;
    }

    return 0;
}
