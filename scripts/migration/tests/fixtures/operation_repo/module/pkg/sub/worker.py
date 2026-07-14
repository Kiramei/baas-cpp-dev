import core.image as image
import core.picture as module_picture
import core.picture as conditional_alias
from .. import helpers as parent_helpers
from . import local


module_picture.co_detect()
module_picture = object()
module_picture.co_detect()
parent_helpers.run()
local.go()

if condition:
    conditional_alias = object()
conditional_alias.co_detect()

if condition:
    import core.picture as identical_alias
else:
    import core.picture as identical_alias
identical_alias.co_detect()


def parameter_shadow(image):
    image.detect()


def function_local_rebinding():
    image.detect()
    image = object()
    return image


def outer_alias_survives_other_scope():
    image.detect()


def comprehension_shadow_does_not_leak():
    values = [image.detect() for image in []]
    image.detect()
    return values


def scoped_import_rebinding():
    import core.picture as scoped_picture

    scoped_picture.co_detect()
    scoped_picture = object()
    scoped_picture.co_detect()
