/* Run in a private directory. No hardware is opened by the tested functions. */
#define CHARGE_POLICY "policy"
#define CHARGE_LOCK "lock"
#define CHARGE_GAME "game.pid"
#define CHARGE_BL "panels"
#define main bananad_main
#include "../bananad/bananad.c"
#undef main
#include <assert.h>
static void put(const char *path, const char *value)
{
	FILE *f = fopen(path, "w"); assert(f); assert(fputs(value, f) >= 0); assert(!fclose(f));
}
static void expect(int closed, const char *expected)
{
	put(CHARGE_POLICY, "9 1");
	assert(idle_charge(closed || charge_dimmed()));
	char buf[64] = {0}; FILE *f = fopen(CHARGE_POLICY, "r"); assert(f);
	assert(fgets(buf, sizeof buf, f)); fclose(f);
	assert(!strcmp(buf, expected));
}
int main(void)
{
	assert(!mkdir("panels", 0700));
	assert(!mkdir("panels/backlight0", 0700));
	assert(!mkdir("panels/backlight1", 0700));
	put("panels/backlight0/bl_power", "0"); put("panels/backlight1/bl_power", "0");
	put("panels/backlight0/brightness", "30"); put("panels/backlight1/brightness", "30");
	expect(0,"busy 9"); expect(1,"idle 9");
	dimmed = 1; cfg.dim_pct = 30; dim_count = 2;
	for (int i=0; i<2; i++) { strcpy(dim_name[i], BL[i]); dim_level[i]=100; }
	expect(0,"idle 9");
	put("panels/backlight0/brightness","100"); expect(0,"busy 9");
	put("panels/backlight0/bl_power","4"); expect(0,"idle 9");
	put(CHARGE_GAME, "123"); expect(1,"busy 9"); expect(0,"busy 9");
	unlink(CHARGE_GAME);
	int fd=open(CHARGE_LOCK,O_RDWR); assert(fd>=0 && !flock(fd,LOCK_EX)); expect(1,"9 1"); close(fd);
	put(CHARGE_POLICY,"9 0"); assert(!idle_charge(1));
	put(CHARGE_POLICY,"broken"); assert(!idle_charge(1));
	puts("Idle policy: open, dim, second panel bright/off, closed, game, launch lock, unarmed and unreadable state passed.");
}
