; WIP, syntax playground
; structures
(struct ray origin dir)
(define my-ray (ray (vec3 0 0 0) (vec3 0 0 1)))
(define my-ray-origin (ray-origin my-ray))
(define my-ray-dir (ray-dir my-ray))

; variants
(variant my-variant Nil num)
(my-variant 0) ; constructs variant with value 0
(my-variant nil) ; constructs variant with value nil

(define var-funcc (func (x) (case x
	((num x) x)
	((Nil) 0)
)))

; maybe allow optional type annotations?
(func ((x my-variant) (y num)) e)

