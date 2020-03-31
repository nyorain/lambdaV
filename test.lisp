(define nat-fold (func (n accum func) (
	let ((body (rec-func (n accum) (
			if (eq n 0)
				accum
				(rec (- n 1) (func accum n))
		))))
		(body n accum)
)))

; (define plus2 (func (x) (plus x 2)))
; (define plusc (func (x) (func (y) (+ x y))))
; (define plus (func (x y) ((plusc x) y)))

(define sumup-accum (func (accum n) (+ accum n)))
; sums up all natural numbers up to x
(define sumup (func (x) (nat-fold x 0 sumup-accum)))

(define twice (func (f) (func (x) (f (f x)))))
(define add1 (func (x) (+ x 1)))
(define add2 (twice add1))

(define val (add2 (sumup 7)))
(define white (vec4 val 1.0 0.4 1.0))
(output 0 white)

