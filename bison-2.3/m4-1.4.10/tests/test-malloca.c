/* Test of safe automatic memory allocation.
   Copyright (C) 2005, 2007 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Written by Bruno Haible <bruno@clisp.org>, 2005.  */

#include <config.h>

#include "malloca.h"

#include <stdlib.h>

static void
do_allocation (int n)
{
  void *ptr = malloca (n);
  freea (ptr);
  ptr = safe_alloca (n);
}

void (*func) (int) = do_allocation;

int
main ()
{
  int i;

  /* Repeat a lot of times, to make sure there's no memory leak.  */
  for (i = 0; i < 50000; i++)
    {
      /* Try various values.
         n = 0 gave a crash on Alpha with gcc-2.5.8.
         Some versions of MacOS X have a stack size limit of 512 KB.  */
      func (34);
      func (134);
      func (399);
      func (510823);
      func (129321);
      func (0);
      func (4070);
      func (4095);
      func (1);
      func (16582);
    }

  return 0;
}
