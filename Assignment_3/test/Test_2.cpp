// LoopInvariantCodeMotion Test-2

int foo(int n0, int n1, int n2, int n3, int n4, int n5){

    while (true){
        //loopInvariant Spostabili
        n0 = n1 + n2; 
        n0 = n1 * 3; 

        if ( n5 == 0 ){
            n4 = n5 + 6;  
        }
        else if ( n5 == 2 ) {
            n2 = 10 + n0;
            break;
        }

        n3 = n2 + 25 ;

    }

    int result = n1 + 5;
    return result;

}