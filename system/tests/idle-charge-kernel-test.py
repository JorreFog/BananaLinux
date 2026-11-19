"""Run the candidate driver's actual policy callbacks with mocked registers, time and IRQ state."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
patch = Path(sys.argv[1]) if len(sys.argv) > 1 else next(
    (root / "buildroot/board/rgds/linux-patches").glob("*003-idle-policy.patch")
)
# These sysfs callbacks are new functions in the patch. Compile the literal
# function bodies with only hardware I/O, the timer and locking stubbed out.
s = "\n".join(line[1:] for line in patch.read_text().splitlines() if line.startswith("+"))
def fn(prefix):
 start=s.index(prefix)
 return s[start:s.index('\n}',start)+2]
work = tempfile.TemporaryDirectory()
p = Path(work.name) / 'policy.c'
p.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
struct work_struct {int unused;}; struct delayed_work {int unused;};
struct rk817_charger {int input_lock; unsigned long policy_epoch; bool policy_armed,policy_may_be_high,plugged_in; struct delayed_work policy_timeout;} fixture;
struct device {int unused;}; struct device_attribute {int unused;};
#define guard(x) (void)
#define dev_get_drvdata(x) (&fixture)
#define container_of(a,b,d) (&fixture)
#define sysfs_emit sprintf
#define HZ 100
#define system_percpu_wq 0
static int level=1500000,timeout,fail_reset,fail_renew;
static void mod_delayed_work(int q,struct delayed_work *d,int ticks){timeout=ticks;}
static int rk817_chg_set_input_current(struct rk817_charger *c,int v){if(fail_renew)return -EIO;level=v;return 0;}
static int rk817_chg_write_default_input(struct rk817_charger *c){if(fail_reset)return -EIO;level=1500000;c->policy_may_be_high=false;return 0;}
'''+ '\n'.join(fn(x) for x in ('static void rk817_policy_timeout(', 'static ssize_t idle_charge_show(', 'static ssize_t idle_charge_store('))+r'''
static ssize_t send(const char*s){return idle_charge_store(0,0,s,strlen(s));}
int main(void){
 char out[64]; fixture.policy_epoch=8;
 assert(send("arm 8")==-ESTALE); fixture.plugged_in=true;
 assert(send("idle 8")==-EPERM); assert(send("arm 7")==-ESTALE);
 assert(send("arm 8 extra")==-EINVAL); assert(send("arm 8")==5 && level==1500000);
 assert(idle_charge_show(0,0,out)>0 && !strcmp(out,"8 1\n"));
 assert(send("idle 8")==6 && level==2000000 && timeout==3*HZ);
 assert(send("busy 8")==6 && level==1500000);
 assert(send("idle 8")==6 && level==2000000);
 /* Failed thermal/register read on renewal: do not renew the old high lease. */
 fail_renew=1;
 assert(send("idle 8")==-EIO && level==1500000 && !fixture.policy_armed);
 assert(send("idle 8")==-ESTALE && send("idle 9")==-EPERM);
 fail_renew=0;assert(send("arm 9")==5 && send("idle 9")==6);
 /* Neither renewing nor returning to normal works. Retry the reset and
  * reject rearming while the hardware may still be high. */
 fail_renew=fail_reset=1;assert(send("idle 9")==-EIO);
 assert(!fixture.policy_armed && fixture.policy_may_be_high && timeout==HZ);
 assert(send("arm 10")==-EAGAIN);
 fail_renew=fail_reset=0;rk817_policy_timeout(0);
 assert(level==1500000 && send("arm 10")==6 && send("idle 10")==7);
 rk817_policy_timeout(0);assert(level==1500000 && !fixture.policy_may_be_high);
 puts("Kernel callback fixtures passed: connection generation, explicit arming, busy/idle, timeout, reset retry and stale writes.");
}
''')
exe = Path(work.name) / "policy-test"
subprocess.run([os.getenv("HOSTCC", "cc"), "-O1", str(p), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
