.text
.p2align 2
.global akl_hook_fixture
.hidden akl_hook_fixture
.type akl_hook_fixture, %function
akl_hook_fixture:
    adrp x9, akl_fixture_value
    ldr w9, [x9, :lo12:akl_fixture_value]
    add w0, w0, w9
    ret
.size akl_hook_fixture, .-akl_hook_fixture
.data
.p2align 2
.hidden akl_fixture_value
akl_fixture_value:
    .word 7
.section .note.GNU-stack,"",%progbits
