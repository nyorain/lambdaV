links:
- https://boxbase.org/entries/2017/oct/2/spirv-target/
- https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html

```
(define uff (func (x) (func (y) (+ x y))))
(define foo (uff 5))
(define val (foo 1)) # defined as 6
```
