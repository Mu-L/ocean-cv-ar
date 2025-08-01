/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

package com.meta.ocean.platform.android.application;

import com.meta.ocean.base.BaseJni;
import com.meta.ocean.platform.android.*;

import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;

import androidx.appcompat.app.AppCompatActivity;

import java.util.Set;
import java.util.HashSet;

/**
 * This class implements the base class for all Ocean activities based on AppCompatActivity.
 * @see OceanActivity.
 * @ingroup platformandroid
 */
public class OceanAppCompatActivity extends AppCompatActivity
{
	protected void onCreate(Bundle savedInstanceState)
	{
		Log.d("Ocean", "OceanAppCompatActivity::onCreate()");

		super.onCreate(savedInstanceState);
		android.os.Process.setThreadPriority(-20);

		BaseJni.initializeWithMessageOutput(messageOutput_, messageOutputFile_);
		BaseJni.information("Device name: " + android.os.Build.DEVICE);
		BaseJni.setWorkerPoolCapacity(2);
	}

	public void requestPermission(String permission)
	{
		if (checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED)
		{
			onPermissionGranted(permission);
		}
		else
		{
			Log.i("Ocean", "Requested permission: " + permission);

			if (pendingPermissionRequests_.add(permission))
			{
				if (pendingPermissionRequests_.size() == 1)
				{
					requestPermissions(new String[]{permission}, OCEAN_ACTIVITY_PERMISSION_CODE);
				}
			}
		}
	}

	@Override
	public void onRequestPermissionsResult(int requestCode, String permissions[], int[] grantResults)
	{
		super.onRequestPermissionsResult(requestCode, permissions, grantResults);

		if (requestCode != OCEAN_ACTIVITY_PERMISSION_CODE)
		{
			return;
		}

		if (grantResults.length == 0)
		{
			// Empty permissions and results arrays should be treated as a cancellation.
			Log.w("Ocean", "A permission request was interrupted/canceled.");
			return;
		}

		for (int n = 0; n < grantResults.length; ++n)
		{
			if (grantResults[n] == PackageManager.PERMISSION_GRANTED)
			{
				Log.i("Ocean", "Granted permission: " + permissions[n]);

				onPermissionGranted(permissions[n]);
			}
			else
			{
				Log.w("Ocean", "Not granted permission: " + permissions[n]);
			}

			pendingPermissionRequests_.remove(permissions[n]);
		}

		if (pendingPermissionRequests_.size() >= 1)
		{
			requestPermissions(new String[]{pendingPermissionRequests_.iterator().next()}, OCEAN_ACTIVITY_PERMISSION_CODE);
		}
	}

	protected void onPermissionGranted(String permission)
	{
		// nothing to do here
	}

	/**
	 * Perform any final cleanup before an activity is destroyed.
	 * This can happen either because the activity is finishing (someone called finish() on it), or because the system is temporarily destroying this instance of the activity to save space.
	 * You can distinguish between these two scenarios with the isFinishing() method.
	 */
	@Override
	protected void onDestroy()
	{
		Log.d("Ocean", "OceanActivity::onDestroy()");

		super.onDestroy();
	}

	/// The output type for all log messages.
	protected int messageOutput_ = BaseJni.MessageOutput.OUTPUT_STANDARD.value();

	/// The file of the output message, STANDARD to use Logcat, empty to cache all messages.
	protected String messageOutputFile_ = "";

	/// The set of all pending permissions.
	private Set<String> pendingPermissionRequests_ = new HashSet<String>();

	// The unique id for permission requests.
	private static final int OCEAN_ACTIVITY_PERMISSION_CODE = 0;
}
