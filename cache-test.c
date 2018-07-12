/*
 * cache-test.c
 *
 * Made: August 6, 2015 by jkasbeer
 *
 * Test web cache
 */

#include <stdio.h>
#include "csapp.h"
#include "pcache.h"

int main()
{
  cache *C;
  C = Malloc(sizeof(struct web_cache));
  /* Test cache_init */
  cache_init(C);
  printf("Testing cache_init...\n");
  print_cache(C);

  /* Test cache_full (1) */
  int full = cache_full(C);
  printf("Testing cache_full...\n");
  printf("Cache is full: %d\n", full);

  /* Add to cache */
  // char *h1, *p1, *o1;
  // h1 = "host1";
  // p1 = "path1";
  // o1 = "object1";
  // char *h2, *p2, *o2;
  // h2 = "host2";
  // p2 = "path2";
  // o2 = "object2";

  // line *line1 = make_line(h1, p1, o1, 8);
  // line *line2 = make_line(h2, p2, o2, 8);
  // add_line(C, line1);
  // print_cache(C);
  // add_line(C, line2);
  // print_cache(C);

  return 0;
}






