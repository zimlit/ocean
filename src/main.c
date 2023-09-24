/**
 * Copyright (C) 2023 Devin Rockwell
 *
 * This file is part of ocean.
 *
 * ocean is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ocean is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ocean.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ncurses.h>

typedef unsigned int Bool;

void
init(void)
{
  initscr();
  raw();
  noecho();
  keypad(stdscr, TRUE);
}

int
main(void)
{
  Bool running = true;

  init();

  while (running == true)
    {
      char input = getch();
      if (input == 'q')
        {
          running = false;
        }
      refresh();
    }

  endwin();

  return 0;
}
