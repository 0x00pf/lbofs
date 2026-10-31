#include <stdio.h>
#include <stdlib.h>

#define NEXT_ROUND 0
#define COMBAT 1
#define DIE 2

struct entity_t { int  hp; int  xp;};
struct game_t {struct entity_t p; struct entity_t m; int a;} g;
void print_status (struct game_t *_g) {
  printf ("  PLAYER  HP:%d XP:%d\n  MONSTER HP:%d XP:%d\n", _g->p.hp, _g->p.xp, _g->m.hp, _g->m.xp);
}
void init_scene (struct game_t *_g) {
  _g->m.hp = 1 + rand() % 99;
  _g->m.xp = 2 + rand () % 8;
  puts ("---------------\n+ New Monster SPAWNED");
}
int game_step (struct game_t *_g) {
  if (_g->a == 'f') {puts ("! YOU FLY. YOU COWARD!"); _g->p.xp = _g->p.xp > 2? _g->p.xp -=2: 2; return NEXT_ROUND;}
  else if (_g->a == 'a') {
    int a1 = rand() % _g->m.xp;
    int a2 = rand() % _g->p.xp;
    printf ("+ MONSTER ATTACK %d\n+ PLAYER  ATTACK %d\n", a1, a2);
    _g->p.hp -= a1; _g->m.hp -= a2;
    print_status (_g);
    if (_g->p.hp <= 0) {puts ("! YOU DIED."); return DIE;}
    if (_g->m.hp <= 0) {puts ("! YOU WON. Moster DIED"); _g->p.hp=100; _g->p.xp+=10;return NEXT_ROUND;}
    return 1;
  }
  return COMBAT;
}
int main () {
  g.p.hp = 100; g.p.xp =10;
  g.a = NEXT_ROUND;
  printf ("picoRPG 0.1\n-----------\n\n");
  printf ("%p\n", game_step);
  while (g.a != DIE) {
    init_scene (&g);
    print_status (&g);
    g.a = COMBAT;
    while (g.a == COMBAT) {
      printf ("* (a)ttack (f)ly ?\n");
      g.a = getchar ();
      if (g.a!= '\n') getchar ();
      g.a = game_step (&g);
    }
  }
  return 0;
}
