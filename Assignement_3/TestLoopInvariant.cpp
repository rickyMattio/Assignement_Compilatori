#include <iostream>

void test_loop_1(int *A, int N) {
    int c = 5;           // costante loop-invariant
    int d = c + 3;       // anche questa lo è
    int e = d * 2;       // dipende da due loop-invariant

    for (int i = 0; i < N; ++i) {
        A[i] = A[i] + e; // e è loop-invariant → può essere spostata
    }
}

void test_loop_2(int *B, int N, int x) {
    int fixed = x + 10;  // x viene da fuori, quindi anche fixed è loop-invariant

    for (int i = 0; i < N; ++i) {
        int temp = B[i] * 2;       // dipende da loop → non spostabile
        B[i] = temp + fixed;       // uso misto: fixed sì, temp no
    }
}

int main() {
    int A[10] = {0};
    int B[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    test_loop_1(A, 10);
    test_loop_2(B, 10, 2);

    std::cout << "Test conclusi\n";
    return 0;
}