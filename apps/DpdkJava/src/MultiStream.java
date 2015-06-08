import java.io.IOException;
import java.io.OutputStream;

/*
 * Output stream to redirect printlns to 2 streams
 * Generally used for streams to console and gui
 */

public class MultiStream extends OutputStream {
	
	private OutputStream ps1;
	private OutputStream ps2;
	
	public MultiStream(OutputStream ps1, OutputStream ps2) {
		this.ps1 = ps1;
		this.ps2 = ps2;
	}

	@Override
	public void write(int b) {
		try {
			ps1.write(b);
		} catch (IOException e) {
			e.printStackTrace();
		}
		try {
			ps2.write(b);
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

}
