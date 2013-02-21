#!/usr/bin/python

import subprocess
import sys

def run_server(server):
  p = subprocess.Popen("./" + server + " < test/gen_test", shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
  time = float(p.stdout.readlines()[-3].split(' ')[1])
  p.wait()
  return time

"""
Calculate mean and standard deviation of data x[]:
    mean = {\sum_i x_i \over n}
    std = sqrt(\sum_i (x_i - mean)^2 \over n-1)
"""
def meanstdv(x):
  from math import sqrt
  n, mean, std = len(x), 0, 0
  for a in x:
    mean = mean + a
  mean = mean / float(n)
  for a in x:
      std = std + (a - mean)**2
  std = sqrt(std / float(n-1))
  return mean, std

def main():
  servers = ["server_coarse", "server_rw", "server_fine"]
  times = {}
  n = 1
  pct_done = 0.0
  if len(sys.argv) != 1:
    n = int(sys.argv[1])
  for server in servers:
    times[server] = []
    for i in range(n):
      print "running trial " + str(i) + " on " + server + " (" + str(pct_done) + "%)"
      pct_done += 100.0 * 1.0/(n * len(servers))
      time = run_server(server)
      times[server].append(time)

  print "raw results:"
  for server, time_list in times.iteritems():
    for i, time_entry in enumerate(time_list):
      print "(" + str(i) + ") " + server + ": " + str(time_entry)
  for server, time_list in times.iteritems():
    if len(time_list) == 1:
      sys.exit(0)
  print "statistics:"
  for server, time_list in times.iteritems():
    mean, stdv = meanstdv(time_list)
    print server + "\t mean: " + str(mean) + "\t standard deviation: " + str(stdv)

if __name__ == "__main__":
  main()
