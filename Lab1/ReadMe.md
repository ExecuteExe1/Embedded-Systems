# Assembly Hash Algorithm

A custom hash algorithm implemented in assembly language that processes strings according to specific transformation rules.

## Algorithm Description

### 1. Hash Initialization
- The hash value starts with a length equal to the input string length.

### 2. Character Processing Rules

#### **Uppercase Latin Letters (A-Z)**
- Add: `(ASCII value) × 2`

#### **Lowercase Latin Letters (a-z)**
- Add: `(distance from 'a')²`
  - Where distance = `ASCII(character) - ASCII('a')`

#### **Digits (0-9)**
Use the following mapping table:

0 → 5
1 → 12
2 → 7
3 → 6
4 → 4
5 → 11
6 → 6
7 → 3
8 → 10
9 → 32


#### **Other Characters**
- All other characters are ignored (no contribution to hash).

### 3. Final Hash Processing

#### **If Hash ≤ 9**
- Use the hash value directly.

#### **If Hash > 9**
1. **Digit Summation**: Sum all digits of the hash value recursively until a single digit is obtained.
   - Example: `164 → 1+6+4 = 11`

2. **Modulo Reduction**: Calculate `hash % 7` until result is in range `[0, 9]`
   - Example: `11 % 7 = 4`

3. **Fibonacci Transformation**: Compute the n-th Fibonacci number where `n` is the reduced value:
   ```c
   int fibonacci(int n) {
       if (n == 0) return 0;
       else if (n == 1) return 1;
       else return fibonacci(n-1) + fibonacci(n-2);
   
