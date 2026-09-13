jQuery( function ( $ ) {
	'use strict';

	var frame;

	$( '#cm_pdf_upload_button' ).on( 'click', function ( event ) {
		event.preventDefault();

		if ( frame ) {
			frame.open();
			return;
		}

		frame = wp.media( {
			title: 'Select PDF',
			button: { text: 'Use this PDF' },
			library: { type: 'application/pdf' },
			multiple: false,
		} );

		frame.on( 'select', function () {
			var attachment = frame.state().get( 'selection' ).first().toJSON();
			$( '#cm_pdf_id' ).val( attachment.id );
			$( '#cm_pdf_filename' ).html(
				'<a href="' + attachment.url + '" target="_blank" rel="noopener noreferrer">' + attachment.filename + '</a>'
			);
			$( '#cm_pdf_remove_button' ).show();
		} );

		frame.open();
	} );

	$( '#cm_pdf_remove_button' ).on( 'click', function ( event ) {
		event.preventDefault();
		$( '#cm_pdf_id' ).val( '' );
		$( '#cm_pdf_filename' ).empty();
		$( this ).hide();
	} );
} );
