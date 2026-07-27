package consumer;

import com.nouprax.tex.core.Document;
import com.nouprax.tex.core.model.RenderTree;

public final class Main {
    private Main() {}

    public static void main(String[] args) {
        RenderTree tree = Document.INSTANCE.compile("The 42 rows, 3.14 total.\n");
        if (tree.getRoot().getChildren().isEmpty()) {
            throw new IllegalStateException("Document.compile returned an empty galley");
        }
        String dump = tree.dump();
        if (!dump.startsWith("render-tree 3\n")) {
            throw new IllegalStateException("native payload returned an unexpected tree: " + dump);
        }
    }
}
