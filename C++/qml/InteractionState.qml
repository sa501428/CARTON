pragma Singleton
import QtQml

QtObject {
    property string tool: "pan"
    function toolForModifiers(modifiers) {
        if (modifiers & Qt.ShiftModifier)
            return "annotate";
        if (modifiers & Qt.AltModifier)
            return "zoom";
        return tool;
    }
}
