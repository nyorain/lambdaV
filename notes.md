links:
- https://boxbase.org/entries/2017/oct/2/spirv-target/
- https://www.khronos.org/registry/spir-v/specs/unified1/SPIRV.html

```
(define uff (func (x) (func (y) (+ x y))))
(define foo (uff 5))
(define val (foo 1)) # defined as 6
```

__trying to unify define and let:__

- just using let sucks since then we would have to allow multiple
  (top-level) expressions in the let body
- just using define sucks because then we have to allow everywhere,
  i.e. defines + expression everywhere

Guess it's not really possible to just use one. Welp, will implement
both then.

# combining recursive functions and first-order functions

```
# we don't allow something like this
(define nat-fold (func (n accum func) (
	if (eq n 0)
		accum
		(rec (- n 1) (func accum n) func)
)))

# but it can simply be transformed to this
(define nat-fold (func (n accum func) (
	(let ((body (rec-func (n accum) (
			if (eq n 0)
				accum
				(rec (- n 1) (func accum n))
		))))
		(body n accum))
)))
```

# dump

```
// Util
// Returns whether the given expression is a first-class function object
bool isFunction(const Expression& expr, const std::vector<Definition>& defs) {
	switch(expr.value.index()) {
		case 0:
		case 1:
		case 4:
			return false;
		case 2: {
			// TODO
		}
		case 3: {
			// check whether it's builtin
			// TODO

			// check if it's known definition
			auto name = std::get<Identifier>(expr.value).name;
			auto it = std::find_if(defs.begin(), defs.end(), [&](auto& def) {
				return def.name == name;
			});
			if(it == defs.end()) {
				throwError(dlg::format("Unknown identifier '{}'", name), expr.loc);
			}

			return isFunction(it->expression, defs);
		}
	}
}

---
