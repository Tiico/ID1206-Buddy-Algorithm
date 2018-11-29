set term png
set output "pagesplot.png"
plot 'allo.dat' using 1:2 title 'Pages Allocated' with lines, \
'allo.dat' using 1:3 title 'Pages used' with lines
