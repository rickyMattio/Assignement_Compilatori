// LoopInvariantCodeMotion Test-3

int function(int n0, int n1, int n2, int n3, int n4) {
    int result = 0;
    
    for (int i = 0; i < 10; i++) {
       //Spostabile 
        n0 = n1 + n2; 

        if (n0 == 27) {
            // LOOPINVARIANT MA NO SPOSTABILE
            n4 = n1 + 7; 
        } else {
            // LOOPINVARIANT MA NO SPOSTABILE
            n4 = 12 - n2;
        }
        //Spostabile
        n3 = n0 + 9;
        // NO LOOPINVARIANT
        result = n4 + 8;
    }

    return result;
}