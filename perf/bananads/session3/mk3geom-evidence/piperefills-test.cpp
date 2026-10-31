#include <cstdio>
typedef unsigned u32;
static inline __attribute__((always_inline)) void PipeRefills(u32 p0, u32 f0, u32 k, u32& p, u32& f, u32& m, bool& due) noexcept
{
    p = p0; f = f0; due = false;
    while (k != 0 && p < 3 && f != 0)
    {
        p--;
        const u32 t = f < 2 ? f : 2;
        p += t;
        f -= t;
        due = true;
        k--;
    }
    if (k != 0)
    {
        // a read leaves 2 or fewer if it is from 3 or fewer: the first, or the second (from 4: then 3)
        due = due || p <= 3 || k >= 2;
        const u32 rem = p + f - k;
        const u32 pp = ((p - k) & 1) ? 3 : 4;
        p = rem < pp ? rem : pp;
        f = rem - p;
    }
    m = f0 - f;
}

int main(){ long bad=0,n=0;
 for(u32 p0=0;p0<=4;p0++) for(u32 f0=0;f0<=256;f0++){
   // the original loop's reads: while p != 0, up to the queue's end; check every stopping point k
   u32 T = p0 ? p0+f0 : 0;
   for(u32 K=0;K<=T;K++){
     u32 p=p0,f=f0,k=0,m=0; bool due=false;
     while(k<K && p!=0){ k++; if(--p<=2){u32 t=f<2?f:2;p+=t;f-=t;m+=t;due=true;} }
     u32 P,F,M; bool D; PipeRefills(p0,f0,k,P,F,M,D); n++;
     if(k!=K||P!=p||F!=f||M!=m||D!=due){ if(bad<10) printf("p0 %u f0 %u K %u k %u: sim %u %u %u %d got %u %u %u %d\n",p0,f0,K,k,p,f,m,due,P,F,M,D); bad++; }
   }}
 printf("cases %ld bad %ld\n",n,bad); }
