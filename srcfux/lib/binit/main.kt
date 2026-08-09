import platform.posix.*
import kotlinx.cinterop.*

// we may use a script that the binit parses (/etc/rc) - milo
// binit is "Bsu's Init!" :3 - milo 

data class Process(
    val executable: String,
    val parameters: String?,
    val priority: Int,
)

enum class Authority { ROOT, ADMIN, CONSUMER, FOREIGNER, NONE }

val startupProcesses = listOf<Process> (
    Process("cowsay", "sup twin", 9999),
)

// hope this helps.
// - milo

// now, i clearly dont have a phd in linux
// but isnt the init process supposed to handle
// rebooting on its own? /gq -- bsu
// you're right, but let's stub things first, and then fill the holes. - milo
// actually use cinterops - milo


// alr -- bsu
// did it - milo
fun SysReboot(arguments: Array<String>) {
    val magic = 0xfee1dead
    val magic2 = 0x28121969
    val cmd = 0x1234567 // LINUX_REBOOT_CMD_RESTART

    val result = syscall(SYS_reboot, magic, magic2, cmd, null)
    if (result == -1) {
        perror("BUDDY, SOMETHING IS NOT RIGHT, HIT CTRL+ALT+DELETE TO FORCE ME REBOOT!")
    }
}

fun main(verbose: Boolean = true) {
    println("Init started.")

    if (startupProcesses.isNotEmpty()) {
        startupProcesses.sortedBy { -(it.priority) }.forEachIndexed { i, p ->
            if (verbose) { println("omw to execute process number $i") }
            if (p.parameters.isNullOrEmpty()) {
                Runtime.getRuntime().exec(p.executable)
            } else {
                Runtime.getRuntime().exec("${p.executable} ${p.parameters}")
            }
        }
    }
}

// didn't know that kotlin is object oriented... - milo
object SysControl {
    @RequiresAuthority(Authority.ROOT)
    fun reboot() {
        // uhhh do the stuff that makes the device reboot n shi
        SysReboot()
    }

    fun wakeGetty() {
        // TODO: Implement getty startup routes (maybe with plaform.posix.eveclvp())
    }
}

annotation class RequiresAuthority(
    val level: Authority = Authority.FOREIGNER
)

// bsu, I made a shared terminal in case you need it
// - milo
// thanks -- bsu