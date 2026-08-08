# Halo 4 storm_fp hand/shoulder/elbow-pole authoring helpers.
#
# Run from Blender's Text Editor. Re-running is safe. The controls drive a
# preview rig; export validates and records their authored world transforms.
bl_info = {
    "name": "Halo 4 VRIK Authoring",
    "author": "Halo MCC VR",
    "version": (1, 0, 0),
    "blender": (4, 3, 0),
    "category": "3D View",
}

import bpy


CONTROL_NAMES = (
    "vrik:right_shoulder",
    "vrik:right_hand",
    "vrik:right_pole",
    "vrik:left_shoulder",
    "vrik:left_hand",
    "vrik:left_pole",
)


def control_objects():
    return [bpy.data.objects.get(name) for name in CONTROL_NAMES]


class H4VRIK_OT_select_control(bpy.types.Operator):
    bl_idname = "h4vrik.select_control"
    bl_label = "Select Halo 4 VRIK control"
    bl_options = {'REGISTER'}

    control_name: bpy.props.StringProperty()

    def execute(self, context):
        obj = bpy.data.objects.get(self.control_name)
        if obj is None:
            return {'CANCELLED'}
        for candidate in context.view_layer.objects:
            candidate.select_set(False)
        obj.hide_set(False)
        obj.select_set(True)
        context.view_layer.objects.active = obj
        return {'FINISHED'}


class H4VRIK_OT_reset_control(bpy.types.Operator):
    bl_idname = "h4vrik.reset_control"
    bl_label = "Reset Halo 4 VRIK control"
    bl_options = {'REGISTER', 'UNDO'}

    control_name: bpy.props.StringProperty()

    def execute(self, context):
        obj = bpy.data.objects.get(self.control_name)
        seed = obj.get("halo4_vrik_seed_matrix") if obj else None
        if obj is None or seed is None or len(seed) != 16:
            return {'CANCELLED'}
        from mathutils import Matrix
        obj.matrix_world = Matrix([seed[row * 4:(row + 1) * 4]
                                   for row in range(4)])
        obj["halo4_needs_user_placement"] = True
        return {'FINISHED'}


class H4VRIK_OT_mark_control(bpy.types.Operator):
    bl_idname = "h4vrik.mark_control"
    bl_label = "Mark Halo 4 VRIK control placed"
    bl_options = {'REGISTER', 'UNDO'}

    control_name: bpy.props.StringProperty()

    def execute(self, context):
        obj = bpy.data.objects.get(self.control_name)
        if obj is None:
            return {'CANCELLED'}
        obj["halo4_needs_user_placement"] = False
        return {'FINISHED'}


class H4VRIK_OT_mark_all(bpy.types.Operator):
    bl_idname = "h4vrik.mark_all"
    bl_label = "Mark all six placed"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        objects = control_objects()
        if any(obj is None for obj in objects):
            return {'CANCELLED'}
        for obj in objects:
            obj["halo4_needs_user_placement"] = False
        return {'FINISHED'}


class H4VRIK_OT_reset_all(bpy.types.Operator):
    bl_idname = "h4vrik.reset_all"
    bl_label = "Reset all six"
    bl_options = {'REGISTER', 'UNDO'}

    def execute(self, context):
        for name in CONTROL_NAMES:
            bpy.ops.h4vrik.reset_control(control_name=name)
        return {'FINISHED'}


class H4VRIK_OT_export(bpy.types.Operator):
    bl_idname = "h4vrik.export_points"
    bl_label = "Export VRIK JSON"
    bl_description = "Run the embedded validated exporter beside this .blend"

    def execute(self, context):
        if not bpy.data.is_saved:
            self.report({'ERROR'}, "Save the .blend before exporting")
            return {'CANCELLED'}
        text = bpy.data.texts.get("parse_halo4_vrik_points.py")
        if text is None:
            self.report({'ERROR'}, "Embedded exporter is missing")
            return {'CANCELLED'}
        namespace = {"__name__": "__main__"}
        try:
            exec(compile(text.as_string(), text.name, "exec"), namespace)
        except SystemExit as exc:
            if exc.code not in (None, 0):
                self.report({'ERROR'}, "Export validation failed; see Console")
                return {'CANCELLED'}
        except Exception as exc:
            self.report({'ERROR'}, str(exc))
            return {'CANCELLED'}
        self.report({'INFO'}, "Exported halo4_vrik_points.json")
        return {'FINISHED'}


class H4VRIK_PT_panel(bpy.types.Panel):
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "Halo 4 VRIK"
    bl_label = "storm_fp IK placement"

    def draw(self, context):
        layout = self.layout
        layout.label(text="Move targets on the visible Chief arms.")
        layout.label(text="X forward, Y left, Z up; scene is metres.")
        for side in ("right", "left"):
            box = layout.box()
            box.label(text=side.title() + " arm")
            for role in ("shoulder", "hand", "pole"):
                name = "vrik:%s_%s" % (side, role)
                obj = bpy.data.objects.get(name)
                if obj is None:
                    box.label(text=name + " MISSING", icon='ERROR')
                    continue
                row = box.row(align=True)
                row.label(text=role.title(), icon=(
                    'ERROR' if obj.get("halo4_needs_user_placement", True)
                    else 'CHECKMARK'))
                select = row.operator(
                    "h4vrik.select_control", text="Select", icon='RESTRICT_SELECT_OFF')
                select.control_name = name
                reset = row.operator("h4vrik.reset_control", text="Reset")
                reset.control_name = name
                if obj.get("halo4_needs_user_placement", True):
                    mark = row.operator("h4vrik.mark_control", text="Placed")
                    mark.control_name = name
                transform = box.column(align=True)
                transform.use_property_split = True
                transform.use_property_decorate = False
                transform.prop(obj, "location", text="Position")
                if role == "hand":
                    transform.prop(obj, "rotation_euler", text="Rotation")
        row = layout.row(align=True)
        row.operator("h4vrik.mark_all", icon='CHECKMARK')
        row.operator("h4vrik.reset_all", icon='LOOP_BACK')
        attachments = [
            obj for obj in bpy.data.objects
            if obj.get("halo4_vrik_attachment_role")]
        if attachments:
            box = layout.box()
            box.label(text="Controller-local attachments")
            for obj in sorted(attachments, key=lambda item: item.name):
                parent = obj.parent.name if obj.parent else "UNPARENTED"
                row = box.row(align=True)
                row.label(text=obj.name, icon=(
                    'CON_CHILDOF' if obj.parent else 'ERROR'))
                select = row.operator(
                    "h4vrik.select_control", text="Select",
                    icon='RESTRICT_SELECT_OFF')
                select.control_name = obj.name
                box.label(text="Parent: " + parent)
                box.label(text="Empty display scale is ignored")
        layout.operator("h4vrik.export_points", icon='EXPORT')
        layout.label(text="Exporter refuses controls not marked Placed.")


CLASSES = (
    H4VRIK_OT_select_control,
    H4VRIK_OT_reset_control,
    H4VRIK_OT_mark_control,
    H4VRIK_OT_mark_all,
    H4VRIK_OT_reset_all,
    H4VRIK_OT_export,
    H4VRIK_PT_panel,
)


def register():
    for cls in reversed(CLASSES):
        if hasattr(bpy.types, cls.__name__):
            try:
                bpy.utils.unregister_class(getattr(bpy.types, cls.__name__))
            except Exception:
                pass
    for cls in CLASSES:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(CLASSES):
        try:
            bpy.utils.unregister_class(cls)
        except Exception:
            pass


if __name__ == "__main__":
    register()
