all: commands coarse rw fine performance

commands:
	pdflatex commands.tex

coarse:
	pdflatex coarse.tex

rw:
	pdflatex rw.tex

fine:
	pdflatex fine.tex

graphs:
	gnuplot < all_graphs.plot

performance: graphs
	pdflatex performance.tex
