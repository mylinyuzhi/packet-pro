import java.lang.reflect.Field;

import sun.misc.Unsafe;

/*
 * Quicker access to java unsafe methods with pointer handling and data conversion
 */

public class UnsafeAccess {
	
	private static final int LONG_SIZE = Long.SIZE / Byte.SIZE;
	private static final int INT_SIZE = Integer.SIZE / Byte.SIZE;
	private static final int SHORT_SIZE = Short.SIZE / Byte.SIZE;
	private static final int BYTE_SIZE = Byte.SIZE / Byte.SIZE;
	
	private static final long MAX_BYTE = 255;
	private static final long MIN_BYTE = 0;
	private static final long MAX_SHORT = 65535;
	private static final long MIN_SHORT = 0;
	private static final long MAX_INT = Long.parseLong("4294967295");
	private static final long MIN_INT = 0;
	//private static final long MAX_LONG = Long.parseLong("18446744073709551615");
	private static final long MAX_LONG = 0;
	private static final long MIN_LONG = 0;
	
	Unsafe unsafe;
	long currentPointer;
	int offset;
	
	public UnsafeAccess() {
		Field f = null;
		try {
			f = Unsafe.class.getDeclaredField("theUnsafe");
		} catch (NoSuchFieldException e) {
			e.printStackTrace();
		} catch (SecurityException e) {
			e.printStackTrace();
		}

        f.setAccessible(true);
        try {
			this.unsafe = (Unsafe)f.get(null);
		} catch (IllegalArgumentException e) {
			e.printStackTrace();
		} catch (IllegalAccessException e) {
			e.printStackTrace();
		}
        currentPointer = 0;
        offset = 0;
	}
	
	public void checkBounds(long value, int byte_size) {
		boolean error = false;
		switch (byte_size) {
			case BYTE_SIZE:
				error = value < MIN_BYTE || value > MAX_BYTE;
				break;
			case SHORT_SIZE:
				error = value < MIN_SHORT || value > MAX_SHORT;
				break;
			case INT_SIZE:
				error = value < MIN_INT || value > MAX_INT;
				break;
			case LONG_SIZE:
				error = value < MIN_LONG || value > MAX_LONG;
				break;
			default:
				break;
		}
		if (error) {
			try {
				throw new SignedToUnsignedOutOfRangeException();
			} catch (SignedToUnsignedOutOfRangeException e) {
				e.printStackTrace();
			}
		}
	}
	
	public void putByte(int value) {
		checkBounds(value, BYTE_SIZE);
		unsafe.putByte(currentPointer + offset, (byte)value);
		offset += BYTE_SIZE;
	}
	
	public void putShort(int value) {
		checkBounds(value, SHORT_SIZE);
		unsafe.putShort(currentPointer + offset, (short)value);
		offset += SHORT_SIZE;
	}
	
	public void putInt(long value) {
		checkBounds(value, INT_SIZE);
		unsafe.putInt(currentPointer + offset, (int)value);
		offset += INT_SIZE;
	}
	
	public void putLong(long value) {
		//checkBounds(value, LONG_SIZE);
		unsafe.putLong(currentPointer + offset, value);
		offset += LONG_SIZE;
	}
	
	public int getShort() {
		short temp = unsafe.getShort(currentPointer + offset);
		offset += SHORT_SIZE;
		return Utils.signToUnsign(temp);
	}
	
	public int getByte() {
		byte temp = unsafe.getByte(currentPointer + offset);
		offset += BYTE_SIZE;
		return Utils.signToUnsign(temp);
	}
	
	public long getInt() {
		int temp = unsafe.getInt(currentPointer + offset);
		offset += INT_SIZE;
		return Utils.signToUnsign(temp);
	}
	
	//TODO: return type of this?
	public long getLong() {
		long temp = unsafe.getLong(currentPointer + offset);
		offset += LONG_SIZE;
		return temp;
	}
	
	public long allocateMemory(long num_of_bytes) {
		return unsafe.allocateMemory(num_of_bytes);
	}

	public void freeMemory(long pointer) {
		unsafe.freeMemory(pointer);
	}
	
	public void setCurrentPointer(long currentPointer) {
		this.currentPointer = currentPointer;
		offset = 0;
	}
	
	public long getCurrentPointer() {
		return currentPointer;
	}
	
	public int getOffset() {
		return offset;
	}
	
	public void setOffset(int offset) {
		this.offset = offset;
	}
	
	public long longSize() {
		return LONG_SIZE;
	}
	
	public int intSize() {
		return INT_SIZE;
	}
	
	public byte byteSize() {
		return BYTE_SIZE;
	}
	
	public short shortSize() {
		return SHORT_SIZE;
	}

}
