#pike __REAL_VERSION__
inherit Tools.Shoot.Test;

constant name="String Creation (existing)";

int k = 500; /* variable to tune the time of the test */
int n;
string file = random_string(255*1024*10);
string file2 = file+"0";
array zz = file/1000;

// This code mostly tests memcmp, actually.
void perform()
{
    int q;
    array ss;
    int z = 1000;

    for( int i=0; i<k; i++ )
    {
        ss = file2/z;
        q += sizeof(ss);
    }
    ss = zz = 0;
    n = q;
}

string present_n(int ntot,int nruns,float tseconds,float useconds,int memusage)
{
   return sprintf("%.0f/s",ntot/useconds);
}
