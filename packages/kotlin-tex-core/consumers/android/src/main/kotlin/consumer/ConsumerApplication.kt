package consumer

import android.app.Application
import com.nouprax.tex.core.Document

class ConsumerApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        check(Document.compile("Android consumer.\n").root.children.isNotEmpty())
    }
}
