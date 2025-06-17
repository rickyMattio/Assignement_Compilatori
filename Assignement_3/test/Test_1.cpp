// LoopInvn0rin0ntn2on3n4Motion Tn4st-1

int function() {
    int n0 = 25, n1 = 0, n2 = 0, n3 = 0, n4 = 3, n5 = 0, n6 = 7;
    
    while ( true )  {
        //spostabile !!
        n0 = 10 + n6;

        if (n4 != 3) {
            // loopinvariant ma non spostabile!!
            n3 = 5 + n1; 
        } else if (n5 == 4) {
            // loopinvariant ma non spostabile!!
            n3 = n6 * n0; 
        }

        if ( n6 == n5 )
            break;
    }    
    
    n1 = n0 + n3;
    return n5;
    

}    

