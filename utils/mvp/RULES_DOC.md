# documentation standards

this project follows specific rules for code documentation to maintain consistency and a clean aesthetic.

## formatting rules

- **language**: all documentation must be in english.
- **casing**: do not use leading capital letters for documentation blocks or sentences within them.
- **punctuation**: do not use periods at the end of paragraphs. periods are allowed within sentences if they are part of a multi-sentence description.
- **style**: documentation should be brief and concise.

## structure

### header files

- **synopsis**: provide a brief synopsis before the definition of each class, struct, or major module.
- **api style**: document every method and function using api-style tags.
    - `@brief`: short description of the function's purpose.
    - `@param`: description of parameters (if applicable).
    - `@return`: description of the return value (if applicable).

### example

```cpp
/**
 * manages tuple storage within a fixed-size disk page
 */
class SlottedPage {
public:
    /**
     * @brief retrieves the page header
     * @return pointer to header
     */
    PageHeader* get_header();
};
```
