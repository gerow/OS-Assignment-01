#!/usr/bin/python

import random

def choose_and_remove( items ):
  # pick an item index
  if items:
    index = random.randrange( len(items) )
    return items.pop(index)
  return None

def main():
  keys = []
  for i in range(5000):
    keys.append(str(i))

  for i in range(50000):
    print "q " + keys[(random.randrange(len(keys)))]

if __name__ == "__main__":
  main()
