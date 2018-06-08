require "array"

a = array.array("b",10)
assert(a:get(1)==false)
assert(a:get(2)==false)
a:set(2,true)
assert(a:get(1)==false)
assert(a:get(2)==true)

a = array.array("b",2,2)
a:set(2,2,true)
assert(a:get(2,2)==true)
a:clear()
assert(a:get(2,2)==false)
a:clear(true)
assert(a:get(2,2)==true)

accum = array.array("b",5)
accum:clear(true)
mask1 = array.array("b",5)
mask2 = array.array("b",5)
mask1:set(2,true)
mask1:set(3,true)
mask2:set(2,true)
accum:band(mask1)
accum:band(mask2)
assert(accum:get(1)==false)
assert(accum:get(2)==true)
assert(accum:get(3)==false)
assert(accum:get(4)==false)
assert(accum:get(5)==false)

ones = array.array("c",10,10)
ones:clear(1)
twos = array.array("c",10,10)
twos:clear(2)
threes = ones:copy()
threes:add(twos)
assert(threes:get(4,4)==3)

toshift = array.array("n",5,5,5)
toshift:set(2,2,2,58.5)
shifted = toshift:shiftcopy(1,2,3)
assert(shifted:get(3,4,5) == 58.5)

print "All tests good."