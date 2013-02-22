clear
reset
set key off
set border 3

# Add a vertical dotted line at x=0 to show centre (mean) of distribution.
set yzeroaxis

# Each bar is half the (visual) width of its x-range.
set boxwidth 0.05 absolute
set style fill solid 1.0 noborder

bin_width = 0.1;

bin_number(x) = floor(x/bin_width)

rounded(x) = bin_width * ( bin_number(x) + 0.5 )

set term png
set style fill solid 0.5
set tics out nomirror
set xlabel "Time (s)"
set ylabel "Frequency"

set output "unbalanced_server_coarse_final.png"
set arrow 1 from 8.3071,0 to 8.3071,1000
set arrow 2 from 7.4207,0 to 7.4207,1000
set arrow 3 from 9.1935,0 to 9.1935,1000
set title "Unbalanced Test -- Coarse Grained Locking"
plot 'unbalanced_server_coarse_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"red"

set output "unbalanced_server_rw_final.png"
set arrow 1 from 7.2918,0 to 7.2918,1000
set arrow 2 from 7.787187,0 to 7.717187,1000
set arrow 3 from 6.76413,0 to 6.796413,1000
set title "Unbalanced Test -- Reader Writer Locking"
plot 'unbalanced_server_rw_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"green"

set output "unbalanced_server_fine_final.png"
set arrow 1 from 5.2562,0 to 5.2562,1000
set arrow 2 from 5.5631,0 to 5.5631,1000
set arrow 3 from 5.95922,0 to 5.94922,1000
set title "Unbalanced Test -- Fine Grained Locking"
plot 'unbalanced_server_fine_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"blue"

set output "gen_server_coarse_final.png"
set arrow 1 from 5.759,0 to 5.759,1000
set arrow 2 from 6.34135,0 to 6.34135,1000
set arrow 3 from 5.17665,0 to 5.17665,1000
set title "Gen Test -- Coarse Grained Locking"
plot 'gen_server_coarse_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"red"

set output "gen_server_rw_final.png"
set arrow 1 from 2.15,0 to 2.15,1000
set arrow 2 from 1.83897,0 to 1.83897,1000
set arrow 3 from 2.4610,0 to 2.4610,1000
set title "Gen Test -- Read Write Locking"
plot 'gen_server_rw_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"green"

set output "gen_server_fine_final.png"
set arrow 1 from 3.0696,0 to 3.0696,1000
set arrow 2 from 3.186095,0 to 3.186095,1000
set arrow 3 from 2.95310423,0 to 2.95310423,1000
set title "Gen Test -- Fine Grained Locking"
plot 'gen_server_fine_final.data' using (rounded($1)):(1) smooth frequency w boxes lc rgb"blue"
