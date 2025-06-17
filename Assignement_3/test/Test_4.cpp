// LoopInvariantCodeMotion Test-4


int function(int n0, int n1, int n2, int n3) {
	for (int i = n0; i > 0; --i) {
        
        if (n1 == 0) break;

        for (int j = n1; j > 0; --j) {
            int x1 = i + 1;
            if (n2 == 0) break;
			for (int k = n2; k > 0; --k) {
				
				int x2 = j + 2;
				if (n3 == 0) break;
				--n3;        
			}
            --n2;  
        }
        --n1;      
    }
	return 0;

}
